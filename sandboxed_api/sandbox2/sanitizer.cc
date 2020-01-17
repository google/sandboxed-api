// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Implementation file for the sandbox2::sanitizer namespace.

#include "sandboxed_api/sandbox2/sanitizer.h"

#if defined(THREAD_SANITIZER)
#include <sanitizer/tsan_interface.h>
#endif

#include <dirent.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <syscall.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <glog/logging.h>
#include "absl/base/internal/raw_logging.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "sandboxed_api/sandbox2/util/file_helpers.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/sandbox2/util/strerror.h"
#include "sandboxed_api/util/raw_logging.h"

namespace sandbox2 {
namespace sanitizer {
namespace {

constexpr char kProcSelfFd[] = "/proc/self/fd";

// Reads filenames inside the directory and converts them to numerical values.
bool ListNumericalDirectoryEntries(const std::string& directory,
                                   std::set<int>* nums) {
  std::vector<std::string> entries;
  std::string error;
  if (!file_util::fileops::ListDirectoryEntries(directory, &entries, &error)) {
    SAPI_RAW_LOG(WARNING, "List directory entries for '%s' failed: %s",
                 kProcSelfFd, error);
    return false;
  }
  for (const auto& entry : entries) {
    int num;
    if (!absl::SimpleAtoi(entry, &num)) {
      SAPI_RAW_LOG(WARNING, "Cannot convert %s to a number", entry);
      return false;
    }
    nums->insert(num);
  }
  return true;
}

// Returns the specified line from /proc/<pid>/status.
std::string GetProcStatusLine(int pid, const std::string& value) {
  const std::string fname = absl::StrCat("/proc/", pid, "/status");
  std::string procpidstatus;
  auto status = file::GetContents(fname, &procpidstatus, file::Defaults());
  if (!status.ok()) {
    SAPI_RAW_LOG(WARNING, "%s", status.message());
    return "";
  }

  for (const auto& line : absl::StrSplit(procpidstatus, '\n')) {
    std::pair<absl::string_view, absl::string_view> kv =
        absl::StrSplit(line, absl::MaxSplits(':', 1));
    SAPI_RAW_VLOG(3, "Key: '%s' Value: '%s'", kv.first, kv.second);
    if (kv.first == value) {
      return std::string(kv.second);
    }
  }
  SAPI_RAW_LOG(ERROR, "No '%s' field found in '%s'", value, fname);
  return "";
}

}  // namespace

bool GetListOfFDs(std::set<int>* fds) {
  if (!ListNumericalDirectoryEntries(kProcSelfFd, fds)) {
    return false;
  }
  // Exclude the dirfd which was opened in ListDirectoryEntries.
  // Most probably will be the highest fd, hence the reverse order.
  for (auto it = fds->rbegin(), end = fds->rend(); it != end; ++it) {
    if (access(absl::StrCat(kProcSelfFd, "/", *it).c_str(), F_OK) != 0) {
      fds->erase(*it);
      break;
    }
  }
  return true;
}

bool GetListOfTasks(int pid, std::set<int>* tasks) {
  const std::string task_dir = absl::StrCat("/proc/", pid, "/task");
  return ListNumericalDirectoryEntries(task_dir, tasks);
}

bool CloseAllFDsExcept(const std::set<int>& fd_exceptions) {
  std::set<int> fds;
  if (!GetListOfFDs(&fds)) {
    return false;
  }

  for (auto fd : fds) {
    if (fd_exceptions.find(fd) != fd_exceptions.end()) {
      continue;
    }
    SAPI_RAW_VLOG(2, "Closing FD:%d", fd);
    close(fd);
  }
  return true;
}

bool MarkAllFDsAsCOEExcept(const std::set<int>& fd_exceptions) {
  std::set<int> fds;
  if (!GetListOfFDs(&fds)) {
    return false;
  }

  for (auto fd : fds) {
    if (fd_exceptions.find(fd) != fd_exceptions.end()) {
      continue;
    }

    SAPI_RAW_VLOG(2, "Marking FD:%d as close-on-exec", fd);

    int flags = fcntl(fd, F_GETFD);
    if (flags == -1) {
      SAPI_RAW_PLOG(ERROR, "fcntl(%d, F_GETFD) failed", fd);
      return false;
    }
    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
      SAPI_RAW_PLOG(ERROR, "fcntl(%d, F_SETFD, %x | FD_CLOEXEC) failed", fd,
                    flags);
      return false;
    }
  }

  return true;
}

int GetNumberOfThreads(int pid) {
  std::string thread_str = GetProcStatusLine(pid, "Threads");
  if (thread_str.empty()) {
    return -1;
  }
  int threads;
  if (!absl::SimpleAtoi(thread_str, &threads)) {
    SAPI_RAW_LOG(ERROR, "Couldn't convert '%s' to a number", thread_str);
    return -1;
  }
  SAPI_RAW_VLOG(1, "Found %d threads in pid: %d", threads, pid);
  return threads;
}

void WaitForTsan() {
#if defined(THREAD_SANITIZER)
  static bool ABSL_ATTRIBUTE_UNUSED dummy_tsan_once = []() {
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

bool SanitizeCurrentProcess(const std::set<int>& fd_exceptions,
                            bool close_fds) {
  SAPI_RAW_VLOG(1, "Sanitizing PID: %d, close_fds: %d", syscall(__NR_getpid),
                close_fds);

  // Put process in a separate session (and a new process group).
  setsid();

  // If the parent goes down, so should we.
  if (prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) != 0) {
    SAPI_RAW_PLOG(ERROR, "prctl(PR_SET_PDEATHSIG, SIGKILL) failed");
    return false;
  }

  // Close or mark as close-on-exec open file descriptors.
  if (close_fds) {
    if (!CloseAllFDsExcept(fd_exceptions)) {
      SAPI_RAW_LOG(ERROR, "Failed to close all fds");
      return false;
    }
  } else {
    if (!MarkAllFDsAsCOEExcept(fd_exceptions)) {
      SAPI_RAW_LOG(ERROR, "Failed to mark all fds as closed");
      return false;
    }
  }
  return true;
}

}  // namespace sanitizer
}  // namespace sandbox2
