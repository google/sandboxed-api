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

#include "sandboxed_api/sandbox2/util.h"

#include <asm/unistd.h>  // __NR_memdfd_create
#include <sched.h>
#include <spawn.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <climits>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/util/file_helpers.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/raw_logging.h"

namespace sandbox2::util {

namespace file = ::sapi::file;
namespace file_util = ::sapi::file_util;

namespace {

std::string ConcatenateAll(char* const* arr) {
  std::string result;
  for (; *arr != nullptr; ++arr) {
    size_t len = strlen(*arr);
    result.append(*arr, len + 1);
  }
  return result;
}

}  // namespace

void CharPtrArrToVecString(char* const* arr, std::vector<std::string>* vec) {
  *vec = CharPtrArray(arr).ToStringVector();
}

const char** VecStringToCharPtrArr(const std::vector<std::string>& vec) {
  const int vec_size = vec.size();
  const char** arr = new const char*[vec_size + 1];
  for (int i = 0; i < vec_size; ++i) {
    arr[i] = vec[i].c_str();
  }
  arr[vec_size] = nullptr;
  return arr;
}

CharPtrArray::CharPtrArray(char* const* arr) : content_(ConcatenateAll(arr)) {
  for (auto it = content_.begin(); it != content_.end();
       it += strlen(&*it) + 1) {
    array_.push_back(&*it);
  }
  array_.push_back(nullptr);
}

CharPtrArray::CharPtrArray(const std::vector<std::string>& vec)
    : content_(absl::StrJoin(vec, absl::string_view("\0", 1))) {
  size_t len = 0;
  array_.reserve(vec.size() + 1);
  for (const std::string& str : vec) {
    array_.push_back(&content_[len]);
    len += str.size() + 1;
  }
  array_.push_back(nullptr);
}

CharPtrArray CharPtrArray::FromStringVector(
    const std::vector<std::string>& vec) {
  return CharPtrArray(vec);
}

std::vector<std::string> CharPtrArray::ToStringVector() const {
  std::vector<std::string> result;
  result.reserve(array_.size() - 1);
  for (size_t i = 0; i < array_.size() - 1; ++i) {
    result.push_back(array_[i]);
  }
  return result;
}

std::string GetProgName(pid_t pid) {
  std::string fname = file::JoinPath("/proc", absl::StrCat(pid), "exe");
  // Use ReadLink instead of RealPath, as for fd-based executables (e.g. created
  // via memfd_create()) the RealPath will not work, as the destination file
  // doesn't exist on the local file-system.
  return file_util::fileops::Basename(file_util::fileops::ReadLink(fname));
}

std::string GetCmdLine(pid_t pid) {
  std::string fname = file::JoinPath("/proc", absl::StrCat(pid), "cmdline");
  std::string cmdline;
  auto status =
      sapi::file::GetContents(fname, &cmdline, sapi::file::Defaults());
  if (!status.ok()) {
    SAPI_RAW_LOG(WARNING, "%s", std::string(status.message()).c_str());
    return "";
  }
  return absl::StrReplaceAll(cmdline, {{absl::string_view("\0", 1), " "}});
}

std::string GetProcStatusLine(int pid, const std::string& value) {
  const std::string fname = absl::StrCat("/proc/", pid, "/status");
  std::string procpidstatus;
  auto status =
      sapi::file::GetContents(fname, &procpidstatus, sapi::file::Defaults());
  if (!status.ok()) {
    SAPI_RAW_LOG(WARNING, "%s", std::string(status.message()).c_str());
    return "";
  }

  for (const auto& line : absl::StrSplit(procpidstatus, '\n')) {
    std::pair<std::string, std::string> kv =
        absl::StrSplit(line, absl::MaxSplits(':', 1));
    SAPI_RAW_VLOG(3, "Key: '%s' Value: '%s'", kv.first.c_str(),
                  kv.second.c_str());
    if (kv.first == value) {
      absl::StripLeadingAsciiWhitespace(&kv.second);
      return std::move(kv.second);
    }
  }
  SAPI_RAW_LOG(ERROR, "No '%s' field found in '%s'", value.c_str(),
               fname.c_str());
  return "";
}

long Syscall(long sys_no,  // NOLINT
             uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4,
             uintptr_t a5, uintptr_t a6) {
  return syscall(sys_no, a1, a2, a3, a4, a5, a6);
}

bool CreateDirRecursive(const std::string& path, mode_t mode) {
  int error = mkdir(path.c_str(), mode);

  if (error == 0 || errno == EEXIST) {
    return true;
  }

  // We couldn't create the dir for reasons we can't handle.
  if (errno != ENOENT) {
    return false;
  }

  // The EEXIST case, the parent directory doesn't exist yet.
  // Let's create it.
  const std::string dir = file_util::fileops::StripBasename(path);
  if (dir == "/" || dir.empty()) {
    return false;
  }
  if (!CreateDirRecursive(dir, mode)) {
    return false;
  }

  // Now the parent dir exists, retry creating the directory.
  error = mkdir(path.c_str(), mode);

  return error == 0;
}

namespace {

int ChildFunc(void* arg) {
  auto* env_ptr = reinterpret_cast<jmp_buf*>(arg);
  // Restore the old stack.
  longjmp(*env_ptr, 1);
}

// This code is inspired by base/process/launch_posix.cc in the Chromium source.
// There are a few things to be careful of here:
// - Make sure the stack_buf is below the env_ptr to please FORTIFY_SOURCE.
// - Make sure the stack_buf is not too far away from the real stack to please
// ASAN. If they are too far away, a warning is printed. This means not only
// that the temporary stack buffer needs to also be on the stack, but also that
// we need to disable ASAN for this function, to prevent it from being placed on
// the fake ASAN stack.
// - Make sure that the buffer is aligned to whatever is required by the CPU.
ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS
ABSL_ATTRIBUTE_NOINLINE
pid_t CloneAndJump(int flags, jmp_buf* env_ptr) {
  uint8_t stack_buf[PTHREAD_STACK_MIN] ABSL_CACHELINE_ALIGNED;
  static_assert(sapi::host_cpu::IsX8664() || sapi::host_cpu::IsPPC64LE() ||
                    sapi::host_cpu::IsArm64() || sapi::host_cpu::IsArm(),
                "Host CPU architecture not supported, see config.h");
  // Stack grows down.
  void* stack = stack_buf + sizeof(stack_buf);
  int r = clone(&ChildFunc, stack, flags, env_ptr, nullptr, nullptr, nullptr);
  if (r == -1) {
    SAPI_RAW_PLOG(ERROR, "clone()");
  }
  return r;
}

}  // namespace

pid_t ForkWithFlags(int flags) {
  const int unsupported_flags = CLONE_CHILD_CLEARTID | CLONE_CHILD_SETTID |
                                CLONE_PARENT_SETTID | CLONE_SETTLS | CLONE_VM;
  if (flags & unsupported_flags) {
    SAPI_RAW_LOG(ERROR, "ForkWithFlags used with unsupported flag");
    return -1;
  }

  jmp_buf env;
  if (setjmp(env) == 0) {
    return CloneAndJump(flags, &env);
  }

  // Child.
  return 0;
}

bool CreateMemFd(int* fd, const char* name) {
  // Usually defined in linux/memfd.h. Define it here to avoid dependency on
  // UAPI headers.
  constexpr uintptr_t MFD_CLOEXEC = 0x0001;
  constexpr uintptr_t MFD_ALLOW_SEALING = 0x0002;
  int tmp_fd = Syscall(__NR_memfd_create, reinterpret_cast<uintptr_t>(name),
                       MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (tmp_fd < 0) {
    if (errno == ENOSYS) {
      SAPI_RAW_LOG(ERROR,
                   "This system does not seem to support the memfd_create()"
                   " syscall. Try running on a newer kernel.");
    } else {
      SAPI_RAW_PLOG(ERROR, "Could not create tmp file '%s'", name);
    }
    return false;
  }
  *fd = tmp_fd;
  return true;
}

absl::StatusOr<int> Communicate(const std::vector<std::string>& argv,
                                const std::vector<std::string>& envv,
                                std::string* output) {
  int cout_pipe[2];
  posix_spawn_file_actions_t action;

  if (pipe(cout_pipe) == -1) {
    return absl::ErrnoToStatus(errno, "creating pipe");
  }
  file_util::fileops::FDCloser cout_closer{cout_pipe[1]};

  posix_spawn_file_actions_init(&action);
  struct ActionCleanup {
    ~ActionCleanup() { posix_spawn_file_actions_destroy(action_); }
    posix_spawn_file_actions_t* action_;
  } action_cleanup{&action};

  // Redirect both stdout and stderr to stdout to our pipe.
  posix_spawn_file_actions_addclose(&action, cout_pipe[0]);
  posix_spawn_file_actions_adddup2(&action, cout_pipe[1], 1);
  posix_spawn_file_actions_adddup2(&action, cout_pipe[1], 2);
  posix_spawn_file_actions_addclose(&action, cout_pipe[1]);

  CharPtrArray args = CharPtrArray::FromStringVector(argv);
  CharPtrArray envp = CharPtrArray::FromStringVector(envv);

  pid_t pid;
  if (posix_spawnp(&pid, args.array()[0], &action, nullptr,
                   const_cast<char**>(args.data()),
                   const_cast<char**>(envp.data())) != 0) {
    return absl::ErrnoToStatus(errno, "posix_spawnp()");
  }

  // Close child end of the pipe.
  cout_closer.Close();

  std::string buffer(1024, '\0');
  for (;;) {
    int bytes_read =
        TEMP_FAILURE_RETRY(read(cout_pipe[0], &buffer[0], buffer.length()));
    if (bytes_read < 0) {
      return absl::ErrnoToStatus(errno, "reading from cout pipe");
    }
    if (bytes_read == 0) {
      break;  // Nothing left to read
    }
    absl::StrAppend(output, absl::string_view(buffer.data(), bytes_read));
  }

  int status;
  SAPI_RAW_PCHECK(TEMP_FAILURE_RETRY(waitpid(pid, &status, 0)) == pid,
                  "Waiting for subprocess");
  return WEXITSTATUS(status);
}

std::string GetSignalName(int signo) {
  constexpr absl::string_view kSignalNames[] = {
      "SIG_0",   "SIGHUP",  "SIGINT",     "SIGQUIT", "SIGILL",    "SIGTRAP",
      "SIGABRT", "SIGBUS",  "SIGFPE",     "SIGKILL", "SIGUSR1",   "SIGSEGV",
      "SIGUSR2", "SIGPIPE", "SIGALRM",    "SIGTERM", "SIGSTKFLT", "SIGCHLD",
      "SIGCONT", "SIGSTOP", "SIGTSTP",    "SIGTTIN", "SIGTTOU",   "SIGURG",
      "SIGXCPU", "SIGXFSZ", "SIGVTALARM", "SIGPROF", "SIGWINCH",  "SIGIO",
      "SIGPWR",  "SIGSYS"};

  if (signo >= SIGRTMIN && signo <= SIGRTMAX) {
    return absl::StrFormat("SIGRT-%d [%d]", signo - SIGRTMIN, signo);
  }
  if (signo < 0 || signo >= static_cast<int>(ABSL_ARRAYSIZE(kSignalNames))) {
    return absl::StrFormat("UNKNOWN_SIGNAL [%d]", signo);
  }
  return absl::StrFormat("%s [%d]", kSignalNames[signo], signo);
}

std::string GetRlimitName(int resource) {
  switch (resource) {
    case RLIMIT_AS:
      return "RLIMIT_AS";
    case RLIMIT_FSIZE:
      return "RLIMIT_FSIZE";
    case RLIMIT_NOFILE:
      return "RLIMIT_NOFILE";
    case RLIMIT_CPU:
      return "RLIMIT_CPU";
    case RLIMIT_CORE:
      return "RLIMIT_CORE";
    default:
      return absl::StrCat("UNKNOWN: ", resource);
  }
}

std::string GetPtraceEventName(int event) {
#if !defined(PTRACE_EVENT_STOP)
#define PTRACE_EVENT_STOP 128
#endif

  switch (event) {
    case PTRACE_EVENT_FORK:
      return "PTRACE_EVENT_FORK";
    case PTRACE_EVENT_VFORK:
      return "PTRACE_EVENT_VFORK";
    case PTRACE_EVENT_CLONE:
      return "PTRACE_EVENT_CLONE";
    case PTRACE_EVENT_EXEC:
      return "PTRACE_EVENT_EXEC";
    case PTRACE_EVENT_VFORK_DONE:
      return "PTRACE_EVENT_VFORK_DONE";
    case PTRACE_EVENT_EXIT:
      return "PTRACE_EVENT_EXIT";
    case PTRACE_EVENT_SECCOMP:
      return "PTRACE_EVENT_SECCOMP";
    case PTRACE_EVENT_STOP:
      return "PTRACE_EVENT_STOP";
    default:
      return absl::StrCat("UNKNOWN: ", event);
  }
}

absl::StatusOr<std::string> ReadCPathFromPid(pid_t pid, uintptr_t ptr) {
  std::string path(PATH_MAX, '\0');
  iovec local_iov[] = {{&path[0], path.size()}};

  static const uintptr_t page_size = getpagesize();
  static const uintptr_t page_mask = ~(page_size - 1);
  // See 'man process_vm_readv' for details on how to read NUL-terminated
  // strings with this syscall.
  size_t len1 = ((ptr + page_size) & page_mask) - ptr;
  len1 = (len1 > path.size()) ? path.size() : len1;
  size_t len2 = (path.size() <= len1) ? 0UL : path.size() - len1;
  // Second iov is wrapping around to NULL ptr.
  if ((ptr + len1) < ptr) {
    len2 = 0UL;
  }

  iovec remote_iov[] = {
      {reinterpret_cast<void*>(ptr), len1},
      {reinterpret_cast<void*>(ptr + len1), len2},
  };

  SAPI_RAW_VLOG(4, "ReadCPathFromPid (iovec): len1: %zu, len2: %zu", len1,
                len2);
  if (process_vm_readv(pid, local_iov, ABSL_ARRAYSIZE(local_iov), remote_iov,
                       ABSL_ARRAYSIZE(remote_iov), 0) < 0) {
    return absl::ErrnoToStatus(
        errno,
        absl::StrFormat("process_vm_readv() failed for PID: %d at address: %#x",
                        pid, reinterpret_cast<uintptr_t>(ptr)));
  }

  // Check for whether there's a NUL byte in the buffer. If not, it's an
  // incorrect path (or >PATH_MAX).
  auto pos = path.find('\0');
  if (pos == std::string::npos) {
    return absl::FailedPreconditionError(absl::StrCat(
        "No NUL-byte inside the C string '", absl::CHexEscape(path), "'"));
  }
  path.resize(pos);
  return path;
}

}  // namespace sandbox2::util
