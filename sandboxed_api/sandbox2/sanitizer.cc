// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Implementation file for the sandbox2::sanitizer namespace.

#include "sandboxed_api/sandbox2/sanitizer.h"

#include <fcntl.h>
#include <sys/prctl.h>
#include <syscall.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/status_macros.h"

#if defined(ABSL_HAVE_ADDRESS_SANITIZER) ||   \
    defined(ABSL_HAVE_HWADDRESS_SANITIZER) || \
    defined(ABSL_HAVE_LEAK_SANITIZER) ||      \
    defined(ABSL_HAVE_MEMORY_SANITIZER) || defined(ABSL_HAVE_THREAD_SANITIZER)
#include <sanitizer/common_interface_defs.h>
#endif

#ifndef __NR_close_range
#define __NR_close_range 436
#endif

namespace sandbox2::sanitizer {
namespace {

namespace file_util = ::sapi::file_util;

constexpr char kProcSelfFd[] = "/proc/self/fd";

// Reads filenames inside the directory and converts them to numerical values.
absl::StatusOr<absl::flat_hash_set<int>> ListNumericalDirectoryEntries(
    const std::string& directory) {
  absl::flat_hash_set<int> result;
  std::vector<std::string> entries;
  std::string error;
  if (!file_util::fileops::ListDirectoryEntries(directory, &entries, &error)) {
    return absl::InternalError(absl::StrCat("List directory entries for '",
                                            directory, "' failed: ", error));
  }
  result.reserve(entries.size());
  for (const auto& entry : entries) {
    int num;
    if (!absl::SimpleAtoi(entry, &num)) {
      return absl::InternalError(
          absl::StrCat("Cannot convert ", entry, " to a number"));
    }
    result.insert(num);
  }
  return result;
}

void CloseRange(unsigned int start, unsigned int end, int flags) {
  // Except of EINVAL's and unshare failures with CLOSE_RANGE_UNSHARE (which we
  // likely won't use) this never fails. So let's just check-fail for simplicity
  // here.
  if (syscall(__NR_close_range, start, end, flags) != 0) {
    SAPI_RAW_PLOG(FATAL, "CloseRange(%d, %d, %d) failed", start, end, flags);
  }
}

void CloseAllFDsExceptWithFlags(absl::Span<const int> fd_exceptions,
                                int flags) {
  if (fd_exceptions.empty()) {
    return CloseRange(0, ~0U, flags);
  }
  std::vector<int> sorted_fd_exceptions(fd_exceptions.begin(),
                                        fd_exceptions.end());
  std::sort(sorted_fd_exceptions.begin(), sorted_fd_exceptions.end());
  unsigned int last_fd = 0;
  for (int fd : sorted_fd_exceptions) {
    if (fd < 0) {
      continue;
    }
    if (last_fd < fd) {
      CloseRange(last_fd, fd - 1, flags);
    }
    last_fd = static_cast<unsigned int>(fd) + 1;
  }
  if (last_fd) {
    CloseRange(last_fd, ~0U, flags);
  }
}

}  // namespace

absl::StatusOr<absl::flat_hash_set<int>> GetListOfFDs() {
  SAPI_ASSIGN_OR_RETURN(absl::flat_hash_set<int> fds,
                        ListNumericalDirectoryEntries(kProcSelfFd));

  //  Exclude the dirfd which was opened in ListDirectoryEntries.
  for (auto it = fds.begin(), end = fds.end(); it != end; ++it) {
    if (faccessat(AT_FDCWD, absl::StrCat(kProcSelfFd, "/", *it).c_str(), F_OK,
                  AT_SYMLINK_NOFOLLOW) != 0) {
      fds.erase(it);
      break;
    }
  }
  return fds;
}

absl::StatusOr<absl::flat_hash_set<int>> GetListOfTasks(int pid) {
  const std::string task_dir = absl::StrCat("/proc/", pid, "/task");
  return ListNumericalDirectoryEntries(task_dir);
}

void CloseAllFDsExcept(absl::Span<const int> fd_exceptions) {
  CloseAllFDsExceptWithFlags(fd_exceptions, 0);
}

void CloseAllFDsExcept(std::initializer_list<int> fd_exceptions) {
  CloseAllFDsExceptWithFlags(absl::Span<const int>(fd_exceptions), 0);
}

absl::Status CloseAllFDsExcept(const absl::flat_hash_set<int>& fd_exceptions) {
  CloseAllFDsExceptWithFlags(
      std::vector<int>(fd_exceptions.begin(), fd_exceptions.end()), 0);
  return absl::OkStatus();
}

absl::Status MarkAllFDsAsCOEExcept(
    const absl::flat_hash_set<int>& fd_exceptions) {
  SAPI_ASSIGN_OR_RETURN(absl::flat_hash_set<int> fds, GetListOfFDs());

  for (const auto& fd : fds) {
    if (fd_exceptions.find(fd) != fd_exceptions.end()) {
      continue;
    }

    SAPI_RAW_VLOG(2, "Marking FD:%d as close-on-exec", fd);

    int flags = fcntl(fd, F_GETFD);
    if (flags == -1) {
      return absl::ErrnoToStatus(
          errno, absl::StrCat("fcntl(", fd, ", F_GETFD) failed"));
    }
    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
      return absl::ErrnoToStatus(
          errno, absl::StrCat("fcntl(", fd, ", F_SETFD, ", flags,
                              " | FD_CLOEXEC) failed"));
    }
  }

  return absl::OkStatus();
}

int GetNumberOfThreads(int pid) {
  std::string thread_str = util::GetProcStatusLine(pid, "Threads");
  if (thread_str.empty()) {
    return -1;
  }
  int threads;
  if (!absl::SimpleAtoi(thread_str, &threads)) {
    SAPI_RAW_LOG(ERROR, "Couldn't convert '%s' to a number",
                 thread_str.c_str());
    return -1;
  }
  SAPI_RAW_VLOG(1, "Found %d threads in pid: %d", threads, pid);
  return threads;
}

void WaitForSanitizer() {
#if defined(ABSL_HAVE_ADDRESS_SANITIZER) ||   \
    defined(ABSL_HAVE_HWADDRESS_SANITIZER) || \
    defined(ABSL_HAVE_LEAK_SANITIZER) ||      \
    defined(ABSL_HAVE_MEMORY_SANITIZER) || defined(ABSL_HAVE_THREAD_SANITIZER)
  static bool ABSL_ATTRIBUTE_UNUSED dummy_once = []() {
    __sanitizer_sandbox_on_notify(nullptr);
    return true;
  }();
  const pid_t pid = getpid();
  int threads;
  for (int retry = 0; retry < 10; ++retry) {
    threads = GetNumberOfThreads(pid);
    if (threads == -1 || threads == 1) {
      break;
    }
    absl::SleepFor(absl::Milliseconds(100));
  }
#endif
}

absl::Status SanitizeCurrentProcess(
    const absl::flat_hash_set<int>& fd_exceptions, bool close_fds) {
  SAPI_RAW_VLOG(1, "Sanitizing PID: %zu, close_fds: %d", syscall(__NR_getpid),
                close_fds);

  // Put process in a separate session (and a new process group).
  setsid();

  // If the parent goes down, so should we.
  if (prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) != 0) {
    return absl::ErrnoToStatus(errno,
                               "prctl(PR_SET_PDEATHSIG, SIGKILL) failed");
  }

  // Close or mark as close-on-exec open file descriptors.
  return close_fds ? CloseAllFDsExcept(fd_exceptions)
                   : MarkAllFDsAsCOEExcept(fd_exceptions);
}

}  // namespace sandbox2::sanitizer
