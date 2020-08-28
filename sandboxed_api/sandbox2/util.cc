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

#include "sandboxed_api/sandbox2/util.h"

#include <asm/unistd.h>  // __NR_memdfd_create
#include <bits/local_lim.h>
#include <sched.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "absl/base/attributes.h"
#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/sandbox2/util/path.h"
#include "sandboxed_api/sandbox2/util/strerror.h"
#include "sandboxed_api/util/raw_logging.h"

namespace sandbox2 {
namespace util {

void CharPtrArrToVecString(char* const* arr, std::vector<std::string>* vec) {
  for (int i = 0; arr[i]; ++i) {
    vec->push_back(arr[i]);
  }
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

std::string GetProgName(pid_t pid) {
  std::string fname = file::JoinPath("/proc", absl::StrCat(pid), "exe");
  // Use ReadLink instead of RealPath, as for fd-based executables (e.g. created
  // via memfd_create()) the RealPath will not work, as the destination file
  // doesn't exist on the local file-system.
  return file_util::fileops::Basename(file_util::fileops::ReadLink(fname));
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
#if defined(__x86_64__) || defined(__x86__) || defined(__i386__) || \
    defined(__powerpc64__)
  // Stack grows down.
  void* stack = stack_buf + sizeof(stack_buf);
#else
#error "Architecture is not supported"
#endif
  int r;
  {
    r = clone(&ChildFunc, stack, flags, env_ptr, nullptr, nullptr, nullptr);
  }
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
  constexpr uintptr_t MFD_CLOEXEC = 0x0001U;
  int tmp_fd = Syscall(__NR_memfd_create, reinterpret_cast<uintptr_t>(name),
                       MFD_CLOEXEC);
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

sapi::StatusOr<int> Communicate(const std::vector<std::string>& argv,
                                const std::vector<std::string>& envv,
                                std::string* output) {
  int cout_pipe[2];
  posix_spawn_file_actions_t action;

  if (pipe(cout_pipe) == -1) {
    return absl::UnknownError(absl::StrCat("creating pipe: ", StrError(errno)));
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

  char** args = const_cast<char**>(util::VecStringToCharPtrArr(argv));
  char** envp = const_cast<char**>(util::VecStringToCharPtrArr(envv));
  struct ArgumentCleanup {
    ~ArgumentCleanup() {
      delete[] args_;
      delete[] envp_;
    }
    char** args_;
    char** envp_;
  } args_cleanup{args, envp};

  pid_t pid;
  if (posix_spawnp(&pid, args[0], &action, nullptr, args, envp) != 0) {
    return absl::UnknownError(
        absl::StrCat("posix_spawnp() failed: ", StrError(errno)));
  }

  // Close child end of the pipe.
  cout_closer.Close();

  std::string buffer(1024, '\0');
  for (;;) {
    int bytes_read =
        TEMP_FAILURE_RETRY(read(cout_pipe[0], &buffer[0], buffer.length()));
    if (bytes_read < 0) {
      return absl::InternalError(
          absl::StrCat("reading from cout pipe failed: ", StrError(errno)));
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

sapi::StatusOr<std::string> ReadCPathFromPid(pid_t pid, uintptr_t ptr) {
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

  SAPI_RAW_VLOG(4, "ReadCPathFromPid (iovec): len1: %d, len2: %d", len1, len2);
  ssize_t sz = process_vm_readv(pid, local_iov, ABSL_ARRAYSIZE(local_iov),
                                remote_iov, ABSL_ARRAYSIZE(remote_iov), 0);
  if (sz < 0) {
    return absl::InternalError(absl::StrFormat(
        "process_vm_readv() failed for PID: %d at address: %#x: %s", pid,
        reinterpret_cast<uintptr_t>(ptr), StrError(errno)));
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

}  // namespace util
}  // namespace sandbox2
