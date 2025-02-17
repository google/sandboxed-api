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

#include <fcntl.h>
#include <linux/limits.h>
#include <sched.h>
#include <spawn.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/util/file_helpers.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/status_macros.h"

namespace sandbox2 {
namespace util {

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

#ifdef __ELF__
extern "C" void __gcov_dump() ABSL_ATTRIBUTE_WEAK;
extern "C" void __gcov_flush() ABSL_ATTRIBUTE_WEAK;
extern "C" void __gcov_reset() ABSL_ATTRIBUTE_WEAK;
#endif

void ResetCoverageData() {
#ifdef __ELF__
  if (&__gcov_reset != nullptr) {
    __gcov_reset();
  }
#endif
}

}  // namespace

void DumpCoverageData() {
#ifdef __ELF__
  if (&__gcov_dump != nullptr) {
    SAPI_RAW_LOG(WARNING, "Flushing coverage data (dump)");
    __gcov_dump();
  } else if (&__gcov_flush != nullptr) {
    SAPI_RAW_LOG(WARNING, "Flushing coverage data (flush)");
    __gcov_flush();
  }
#endif
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

absl::StatusOr<std::string> GetResolvedFdLink(pid_t pid, uint32_t fd) {
  // The proc/PID/fd directory contains links for all of that process' file
  // descriptors. They'll show up as more informative strings (paths, sockets).
  std::string fd_path = absl::StrFormat("/proc/%u/fd/%u", pid, fd);
  std::string result(PATH_MAX, '\0');
  ssize_t size = readlink(fd_path.c_str(), &result[0], PATH_MAX);
  if (size < 0) {
    return absl::ErrnoToStatus(size, "failed to read link");
  }
  result.resize(size);
  return result;
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
  constexpr uintptr_t kMfdCloseOnExec = 0x0001;
  constexpr uintptr_t kMfdAllowSealing = 0x0002;
  int tmp_fd = Syscall(__NR_memfd_create, reinterpret_cast<uintptr_t>(name),
                       kMfdCloseOnExec | kMfdAllowSealing);
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

std::string GetAddressFamily(int addr_family) {
  // Taken from definitions in `socket.h`. Each family's index in the array is
  // also its integer value.
  constexpr absl::string_view kAddressFamilies[] = {
      "AF_UNSPEC",     "AF_UNIX",      "AF_INET",     "AF_AX25",
      "AF_IPX",        "AF_APPLETALK", "AF_NETROM",   "AF_BRIDGE",
      "AF_ATMPVC",     "AF_X25",       "AF_INET6",    "AF_ROSE",
      "AF_DECnet",     "AF_NETBEUI",   "AF_SECURITY", "AF_KEY",
      "AF_NETLINK",    "AF_PACKET",    "AF_ASH",      "AF_ECONET",
      "AF_ATMSVC",     "AF_RDS",       "AF_SNA",      "AF_IRDA",
      "AF_PPPOX",      "AF_WANPIPE",   "AF_LLC",      "AF_IB",
      "AF_MPLS",       "AF_CAN",       "AF_TIPC",     "AF_BLUETOOTH",
      "AF_IUCV",       "AF_RXRPC",     "AF_ISDN",     "AF_PHONET",
      "AF_IEEE802154", "AF_CAIF",      "AF_ALG",      "AF_NFC",
      "AF_VSOCK",      "AF_KCM",       "AF_QIPCRTR",  "AF_SMC",
      "AF_XDP",        "AF_MCTP"};

  if (addr_family < 0 && addr_family >= ABSL_ARRAYSIZE(kAddressFamilies)) {
    return absl::StrFormat("UNKNOWN_ADDRESS_FAMILY [%d]", addr_family);
  }
  return std::string(kAddressFamilies[addr_family]);
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

namespace {

// Transfer memory via process_vm_readv/process_vm_writev in page-aligned
// chunks.
absl::StatusOr<size_t> ProcessVmTransfer(bool is_read, pid_t pid, uintptr_t ptr,
                                         absl::Span<char> data) {
  // Input sanity checks.
  if (data.empty()) {
    return 0;
  }

  size_t total_bytes_transferred = 0;
  while (!data.empty()) {
    iovec local_iov = {data.data(), data.size()};
    iovec remote_iov = {reinterpret_cast<void*>(ptr), data.size()};
    ssize_t bytes_transferred =
        is_read ? process_vm_readv(pid, &local_iov, 1, &remote_iov, 1, 0)
                : process_vm_writev(pid, &local_iov, 1, &remote_iov, 1, 0);
    if (bytes_transferred == 0) {
      if (total_bytes_transferred > 0) {
        return total_bytes_transferred;
      }
      return absl::NotFoundError(absl::StrFormat(
          "Transfer was unsuccessful for PID: %d at address: %#x", pid, ptr));
    } else if (bytes_transferred < 0) {
      if (total_bytes_transferred > 0) {
        return total_bytes_transferred;
      }
      return absl::ErrnoToStatus(
          errno,
          absl::StrFormat("transfer() failed for PID: %d at address: %#x", pid,
                          ptr));
    }
    ptr += bytes_transferred;
    data = data.subspan(bytes_transferred, data.size() - bytes_transferred);
    total_bytes_transferred += bytes_transferred;
  }
  return total_bytes_transferred;
}

// Transfer memory via process_vm_readv.
absl::StatusOr<size_t> ProcessVmReadInSplitChunks(pid_t pid, uintptr_t ptr,
                                                  absl::Span<char> data) {
  static const uintptr_t page_size = getpagesize();
  static const uintptr_t page_mask = page_size - 1;

  // Input sanity checks.
  if (data.empty()) {
    return 0;
  }

  // Repeatedly call process_vm_readv/writev in IOV_MAX iovec chunks.
  size_t total_bytes_transferred = 0;
  while (!data.empty()) {
    // Stores all the necessary iovecs to move memory.
    iovec local_iov = {data.data(), 0};
    // Stores all the necessary iovecs to move memory.
    std::vector<iovec> remote_iov;
    // Each iovec should be contained to a single page.
    while (!data.empty() && remote_iov.size() < IOV_MAX) {
      size_t size_in_page = page_size - (ptr & page_mask);
      size_t chunk_size = std::min(data.size(), size_in_page);
      remote_iov.push_back({reinterpret_cast<void*>(ptr), chunk_size});
      local_iov.iov_len += chunk_size;
      ptr += chunk_size;
      data = data.subspan(chunk_size, data.size() - chunk_size);
    }
    ssize_t bytes_transferred = process_vm_readv(
        pid, &local_iov, 1, remote_iov.data(), remote_iov.size(), 0);
    if (bytes_transferred == 0) {
      if (total_bytes_transferred == 0) {
        return absl::NotFoundError(absl::StrFormat(
            "Transfer was unsuccessful for PID: %d at address: %#x", pid, ptr));
      }
      break;
    } else if (bytes_transferred < 0) {
      return absl::ErrnoToStatus(
          errno,
          absl::StrFormat("transfer() failed for PID: %d at address: %#x", pid,
                          ptr));
    }
    total_bytes_transferred += bytes_transferred;
    if (bytes_transferred < local_iov.iov_len) {
      // Read to end of a mapped region (short of full transfer).
      break;
    }
  }
  return total_bytes_transferred;
}

// Open /proc/pid/mem file descriptor.
absl::StatusOr<file_util::fileops::FDCloser> OpenProcMem(pid_t pid,
                                                         bool is_read) {
  auto path = absl::StrFormat("/proc/%d/mem", pid);
  auto closer = file_util::fileops::FDCloser(
      open(path.c_str(), is_read ? O_RDONLY : O_WRONLY));
  if (closer.get() == -1) {
    return absl::ErrnoToStatus(
        errno, absl::StrFormat("open() failed for PID: %d", pid));
  }
  return closer;
}

absl::StatusOr<size_t> ProcMemTransfer(bool is_read, pid_t pid, uintptr_t ptr,
                                       absl::Span<char> data) {
  if (data.empty()) {
    return 0;
  }

  SAPI_ASSIGN_OR_RETURN(file_util::fileops::FDCloser fd_closer,
                        OpenProcMem(pid, is_read));
  size_t total_bytes_transferred = 0;
  while (!data.empty()) {
    ssize_t bytes_transfered =
        is_read ? pread(fd_closer.get(), data.data(), data.size(), ptr)
                : pwrite(fd_closer.get(), data.data(), data.size(), ptr);
    if (bytes_transfered == 0) {
      if (total_bytes_transferred == 0) {
        return absl::NotFoundError(absl::StrFormat(
            "Transfer was unsuccessful for PID: %d at address: %#x", pid, ptr));
      }
      break;
    } else if (bytes_transfered < 0) {
      if (total_bytes_transferred > 0) {
        // Return number of bytes transferred until this error or end.
        break;
      }
      // pread/write of /proc/<pid>mem returns EIO when ptr is unmapped.
      if (errno == EIO) {
        // Emulate returned error code from process_vm_readv.
        errno = EFAULT;
      }
      return absl::ErrnoToStatus(
          errno,
          absl::StrFormat("transfer() failed for PID: %d at address: %#x", pid,
                          ptr));
    }
    ptr += bytes_transfered;
    data = data.subspan(bytes_transfered, data.size() - bytes_transfered);
    total_bytes_transferred += bytes_transfered;
  }
  return total_bytes_transferred;
}

bool CheckIfProcessVmTransferWorks() {
  // Fall-back to pread("/proc/$pid/mem") if process_vm_readv is unavailable.
  static bool process_vm_transfer_works = []() {
    constexpr char kMagic = 42;
    char src = kMagic;
    char dst = 0;
    absl::StatusOr<size_t> read = internal::ReadBytesFromPidWithReadv(
        getpid(), reinterpret_cast<uintptr_t>(&src), absl::MakeSpan(&dst, 1));
    if (!read.ok() || *read != 1 || dst != kMagic) {
      SAPI_RAW_LOG(WARNING,
                   "This system does not seem to support the process_vm_readv()"
                   " or process_vm_writev syscall. Falling back to transfers"
                   " via /proc/pid/mem.");
      return false;
    }
    return true;
  }();
  return process_vm_transfer_works;
}

}  // namespace

namespace internal {

absl::StatusOr<size_t> ReadBytesFromPidWithReadv(pid_t pid, uintptr_t ptr,
                                                 absl::Span<char> data) {
  return ProcessVmTransfer(true, pid, ptr, data);
}

absl::StatusOr<size_t> WriteBytesToPidWithWritev(pid_t pid, uintptr_t ptr,
                                                 absl::Span<const char> data) {
  return ProcessVmTransfer(
      false, pid, ptr,
      absl::MakeSpan(const_cast<char*>(data.data()), data.size()));
}

absl::StatusOr<size_t> ReadBytesFromPidWithProcMem(pid_t pid, uintptr_t ptr,
                                                   absl::Span<char> data) {
  return ProcMemTransfer(true, pid, ptr, data);
}

absl::StatusOr<size_t> ReadBytesFromPidWithReadvInSplitChunks(
    pid_t pid, uintptr_t ptr, absl::Span<char> data) {
  return ProcessVmReadInSplitChunks(pid, ptr, data);
}

absl::StatusOr<size_t> WriteBytesToPidWithProcMem(pid_t pid, uintptr_t ptr,
                                                  absl::Span<const char> data) {
  return ProcMemTransfer(
      false, pid, ptr,
      absl::MakeSpan(const_cast<char*>(data.data()), data.size()));
}

}  // namespace internal

absl::StatusOr<size_t> ReadBytesFromPidInto(pid_t pid, uintptr_t ptr,
                                            absl::Span<char> data) {
  if (CheckIfProcessVmTransferWorks()) {
    return internal::ReadBytesFromPidWithReadv(pid, ptr, data);
  } else {
    return internal::ReadBytesFromPidWithProcMem(pid, ptr, data);
  }
}

absl::StatusOr<size_t> WriteBytesToPidFrom(pid_t pid, uintptr_t ptr,
                                           absl::Span<const char> data) {
  if (CheckIfProcessVmTransferWorks()) {
    return internal::WriteBytesToPidWithWritev(pid, ptr, data);
  } else {
    return internal::WriteBytesToPidWithProcMem(pid, ptr, data);
  }
}

absl::StatusOr<std::vector<uint8_t>> ReadBytesFromPid(pid_t pid, uintptr_t ptr,
                                                      size_t size) {
  // Allocate enough bytes to hold the entire size.
  std::vector<uint8_t> bytes(size, 0);
  size_t result;
  if (CheckIfProcessVmTransferWorks()) {
    SAPI_ASSIGN_OR_RETURN(
        result,
        ProcessVmReadInSplitChunks(
            pid, ptr,
            absl::MakeSpan(reinterpret_cast<char*>(bytes.data()), size)));
  } else {
    SAPI_ASSIGN_OR_RETURN(
        result,
        internal::ReadBytesFromPidWithProcMem(
            pid, ptr,
            absl::MakeSpan(reinterpret_cast<char*>(bytes.data()), size)));
  }
  // Ensure only successfully read bytes are returned.
  bytes.resize(result);
  return bytes;
}

absl::StatusOr<std::string> ReadCPathFromPid(pid_t pid, uintptr_t ptr) {
  SAPI_ASSIGN_OR_RETURN(std::vector<uint8_t> bytes,
                        ReadBytesFromPid(pid, ptr, PATH_MAX));
  auto null_pos = absl::c_find(bytes, '\0');
  std::string path(bytes.begin(), null_pos);
  if (null_pos == bytes.end()) {
    return absl::FailedPreconditionError(
        absl::StrFormat("path '%s' is too long", absl::CHexEscape(path)));
  }
  return path;
}

int Execveat(int dirfd, const char* pathname, const char* const argv[],
             const char* const envp[], int flags, uintptr_t extra_arg) {
  // Flush coverage data prior to exec.
  if (extra_arg == 0) {
    DumpCoverageData();
  }
  int res = syscall(__NR_execveat, static_cast<uintptr_t>(dirfd),
                    reinterpret_cast<uintptr_t>(pathname),
                    reinterpret_cast<uintptr_t>(argv),
                    reinterpret_cast<uintptr_t>(envp),
                    static_cast<uintptr_t>(flags), extra_arg);
  // Reset coverage data if exec fails as the counters have been already dumped.
  if (extra_arg == 0) {
    ResetCoverageData();
  }
  return res;
}

absl::StatusOr<bool> IsRunningInSandbox2() {
  // Check if the kMagicSyscallNo syscall is available.
  int result = Syscall(sandbox2::internal::kMagicSyscallNo);
  if (result == 0) {
    // If this happens, then someone has implemented the kMagicSyscallNo syscall
    // and it is returning 0.
    return absl::InternalError(
        "kMagicSyscallNo syscall succeeded unexpectedly");
  }

  // The caller is not running under a sandbox2.
  if (errno == ENOSYS) {
    return false;
  }

  // The caller is running under a sandbox2.
  if (errno == sandbox2::internal::kMagicSyscallErr) {
    return true;
  }

  // An unexpected errno was returned.
  return absl::InternalError(absl::StrFormat(
      "Unexpected errno for syscall kMagicSyscallNo: %d", errno));
}

}  // namespace util
}  // namespace sandbox2
