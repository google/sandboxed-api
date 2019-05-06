// Copyright 2019 Google LLC. All Rights Reserved.
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

#include "sandboxed_api/sandbox2/policybuilder.h"

#include <asm/ioctls.h>  // For TCGETS
#if defined(__x86_64__)
#include <asm/prctl.h>
#endif
#if defined(__powerpc64__)
#include <asm/termbits.h>  // On PPC, TCGETS macro needs termios
#endif
#include <fcntl.h>  // For the fcntl flags
#include <linux/futex.h>
#include <linux/random.h>  // For GRND_NONBLOCK
#include <sys/mman.h>      // For mmap arguments
#include <syscall.h>

#include <csignal>
#include <cstdint>
#include <utility>

#include <glog/logging.h>
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "sandboxed_api/sandbox2/namespace.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/sandbox2/util/path.h"
#include "sandboxed_api/util/canonical_errors.h"
#include "sandboxed_api/util/status_macros.h"

namespace sandbox2 {
namespace {

}  // namespace

PolicyBuilder& PolicyBuilder::AllowSyscall(unsigned int num) {
  if (handled_syscalls_.insert(num).second) {
    output_->user_policy_.insert(output_->user_policy_.end(),
                                 {SYSCALL(num, ALLOW)});
  }
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowSyscalls(const std::vector<uint32_t>& nums) {
  for (auto num : nums) {
    AllowSyscall(num);
  }
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowSyscalls(SyscallInitializer nums) {
  for (auto num : nums) {
    AllowSyscall(num);
  }
  return *this;
}

PolicyBuilder& PolicyBuilder::BlockSyscallWithErrno(unsigned int num,
                                                    int error) {
  if (handled_syscalls_.insert(num).second) {
    output_->user_policy_.insert(output_->user_policy_.end(),
                                 {SYSCALL(num, ERRNO(error))});
  }
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowExit() {
  return AllowSyscalls({__NR_exit, __NR_exit_group});
}

PolicyBuilder& PolicyBuilder::AllowScudoMalloc() {
  AllowTime();
  AllowSyscalls({__NR_munmap, __NR_nanosleep});
  AllowFutexOp(FUTEX_WAKE);
  AllowLimitedMadvise();
  AllowGetRandom();

  return AddPolicyOnMmap([](bpf_labels& labels) -> std::vector<sock_filter> {
    return {
        ARG_32(2),  // prot
        JEQ32(PROT_NONE, JUMP(&labels, prot_none)),
        JNE32(PROT_READ | PROT_WRITE, JUMP(&labels, mmap_end)),

        // PROT_READ | PROT_WRITE
        ARG_32(3),  // flags
        JEQ32(MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, ALLOW),
        JEQ32(MAP_PRIVATE | MAP_ANONYMOUS, ALLOW),
        JUMP(&labels, mmap_end),

        // PROT_NONE
        LABEL(&labels, prot_none),
        ARG_32(3),  // flags
        JEQ32(MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE, ALLOW),

        LABEL(&labels, mmap_end),
    };
  });
}

PolicyBuilder& PolicyBuilder::AllowTcMalloc() {
  AllowTime();
  AllowSyscalls({__NR_munmap, __NR_nanosleep, __NR_brk, __NR_mincore});
  AllowFutexOp(FUTEX_WAKE);
  AllowLimitedMadvise();
#ifdef __NR_rseq
  AllowSyscall(__NR_rseq);
#endif

  AddPolicyOnSyscall(__NR_mprotect, {
                                        ARG_32(2),
                                        JEQ32(PROT_READ | PROT_WRITE, ALLOW),
                                        JEQ32(PROT_NONE, ALLOW),
                                    });

  return AddPolicyOnMmap([](bpf_labels& labels) -> std::vector<sock_filter> {
    return {
        ARG_32(2),  // prot
        JEQ32(PROT_NONE, JUMP(&labels, prot_none)),
        JNE32(PROT_READ | PROT_WRITE, JUMP(&labels, mmap_end)),

        // PROT_READ | PROT_WRITE
        ARG_32(3),  // flags
        JNE32(MAP_ANONYMOUS | MAP_PRIVATE, JUMP(&labels, mmap_end)),
        ALLOW,

        // PROT_NONE
        LABEL(&labels, prot_none),
        ARG_32(3),  // flags
        JEQ32(MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE, ALLOW),
        JEQ32(MAP_ANONYMOUS | MAP_PRIVATE, ALLOW),

        LABEL(&labels, mmap_end),
    };
  });
}

PolicyBuilder& PolicyBuilder::AllowSystemMalloc() {
  AllowSyscalls({__NR_munmap, __NR_brk});
  AddPolicyOnSyscall(__NR_mremap, {
                                      ARG_32(3),
                                      JEQ32(MREMAP_MAYMOVE, ALLOW),
                                  });
  return AddPolicyOnMmap([](bpf_labels& labels) -> std::vector<sock_filter> {
    return {
        ARG_32(2),  // prot
        JEQ32(PROT_NONE, JUMP(&labels, prot_none)),
        JNE32(PROT_READ | PROT_WRITE, JUMP(&labels, mmap_end)),

        // PROT_READ | PROT_WRITE
        ARG_32(3),  // flags
        JEQ32(MAP_ANONYMOUS | MAP_PRIVATE, ALLOW),

        // PROT_NONE
        LABEL(&labels, prot_none),
        ARG_32(3),  // flags
        JEQ32(MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE, ALLOW),

        LABEL(&labels, mmap_end),
    };
  });

  return *this;
}

PolicyBuilder& PolicyBuilder::AllowLimitedMadvise() {
  return AddPolicyOnSyscall(__NR_madvise, {
                                              ARG_32(2),
                                              JEQ32(MADV_DONTNEED, ALLOW),
                                              JEQ32(MADV_REMOVE, ALLOW),
                                              JEQ32(MADV_NOHUGEPAGE, ALLOW),
                                          });
}

PolicyBuilder& PolicyBuilder::AllowMmap() {
  // Consistently with policy.cc, when mmap2 exists then mmap is denied (not
  // allowed).
#ifdef __NR_mmap2
  return AllowSyscall(__NR_mmap2);
#else
  return AllowSyscall(__NR_mmap);
#endif
}

PolicyBuilder& PolicyBuilder::AllowOpen() {
#ifdef __NR_open
  AllowSyscall(__NR_open);
#endif
#ifdef __NR_openat
  AllowSyscall(__NR_openat);
#endif
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowStat() {
#ifdef __NR_fstat
  AllowSyscall(__NR_fstat);
#endif
#ifdef __NR_fstat64
  AllowSyscall(__NR_fstat64);
#endif
#ifdef __NR_fstatat
  AllowSyscall(__NR_fstatat);
#endif
#ifdef __NR_fstatat64
  AllowSyscall(__NR_fstatat64);
#endif
#ifdef __NR_lstat
  AllowSyscall(__NR_lstat);
#endif
#ifdef __NR_lstat64
  AllowSyscall(__NR_lstat64);
#endif
#ifdef __NR_newfstatat
  AllowSyscall(__NR_newfstatat);
#endif
#ifdef __NR_oldfstat
  AllowSyscall(__NR_oldfstat);
#endif
#ifdef __NR_oldlstat
  AllowSyscall(__NR_oldlstat);
#endif
#ifdef __NR_oldstat
  AllowSyscall(__NR_oldstat);
#endif
#ifdef __NR_stat
  AllowSyscall(__NR_stat);
#endif
#ifdef __NR_stat64
  AllowSyscall(__NR_stat64);
#endif
#ifdef __NR_statfs
  AllowSyscall(__NR_statfs);
#endif
#ifdef __NR_statfs64
  AllowSyscall(__NR_statfs64);
#endif
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowRead() {
  return AllowSyscalls({
      __NR_read,
      __NR_readv,
      __NR_preadv,
      __NR_pread64,
  });
}

PolicyBuilder& PolicyBuilder::AllowWrite() {
  return AllowSyscalls({
      __NR_write,
      __NR_writev,
      __NR_pwritev,
      __NR_pwrite64,
  });
}

PolicyBuilder& PolicyBuilder::AllowReaddir() {
  return AllowSyscalls({
#ifdef __NR_getdents
      __NR_getdents,
#endif
#ifdef __NR_getdents64
      __NR_getdents64,
#endif
  });
}

PolicyBuilder& PolicyBuilder::AllowSafeFcntl() {
  return AddPolicyOnSyscalls({__NR_fcntl,
#ifdef __NR_fcntl64
                              __NR_fcntl64
#endif
                             },
                             {
                                 ARG_32(1),
                                 JEQ32(F_GETFD, ALLOW),
                                 JEQ32(F_SETFD, ALLOW),
                                 JEQ32(F_GETFL, ALLOW),
                                 JEQ32(F_SETFL, ALLOW),
                                 JEQ32(F_GETLK, ALLOW),
                                 JEQ32(F_SETLK, ALLOW),
                                 JEQ32(F_SETLKW, ALLOW),
                                 JEQ32(F_DUPFD, ALLOW),
                                 JEQ32(F_DUPFD_CLOEXEC, ALLOW),
                             });
}

PolicyBuilder& PolicyBuilder::AllowFork() {
  return AllowSyscalls({
#ifdef __NR_fork
      __NR_fork,
#endif
#ifdef __NR_vfork
      __NR_vfork,
#endif
      __NR_clone});
}

PolicyBuilder& PolicyBuilder::AllowWait() {
  return AllowSyscalls({
#ifdef __NR_waitpid
      __NR_waitpid,
#endif
      __NR_wait4});
}

PolicyBuilder& PolicyBuilder::AllowHandleSignals() {
  return AllowSyscalls({
      __NR_rt_sigaction,
      __NR_rt_sigreturn,
      __NR_rt_sigprocmask,
#ifdef __NR_signal
      __NR_signal,
#endif
#ifdef __NR_sigaction
      __NR_sigaction,
#endif
#ifdef __NR_sigreturn
      __NR_sigreturn,
#endif
#ifdef __NR_sigprocmask
      __NR_sigprocmask,
#endif
  });
}

PolicyBuilder& PolicyBuilder::AllowTCGETS() {
  return AddPolicyOnSyscall(__NR_ioctl, {
                                            ARG_32(1),
                                            JEQ32(TCGETS, ALLOW),
                                        });
}

PolicyBuilder& PolicyBuilder::AllowTime() {
  return AllowSyscalls({
#ifdef __NR_time
      __NR_time,
#endif
      __NR_gettimeofday, __NR_clock_gettime});
}

PolicyBuilder& PolicyBuilder::AllowSleep() {
  return AllowSyscalls({
      __NR_clock_nanosleep,
      __NR_nanosleep,
  });
}

PolicyBuilder& PolicyBuilder::AllowGetIDs() {
  return AllowSyscalls({
      __NR_getuid,
      __NR_geteuid,
      __NR_getresuid,
      __NR_getgid,
      __NR_getegid,
      __NR_getresgid,
#ifdef __NR_getuid32
      __NR_getuid32,
      __NR_geteuid32,
      __NR_getresuid32,
      __NR_getgid32,
      __NR_getegid32,
      __NR_getresgid32,
#endif
      __NR_getgroups,
  });
}

PolicyBuilder& PolicyBuilder::AllowGetPIDs() {
  return AllowSyscalls({
      __NR_getpid,
      __NR_getppid,
      __NR_gettid,
  });
}

PolicyBuilder& PolicyBuilder::AllowGetRlimit() {
  return AllowSyscalls({
      __NR_getrlimit,
#ifdef __NR_ugetrlimit
      __NR_ugetrlimit,
#endif
  });
}

PolicyBuilder& PolicyBuilder::AllowSetRlimit() {
  return AllowSyscalls({
      __NR_setrlimit,
#ifdef __NR_usetrlimit
      __NR_usetrlimit,
#endif
  });
}

PolicyBuilder& PolicyBuilder::AllowGetRandom() {
  return AddPolicyOnSyscall(__NR_getrandom, {
                                                ARG_32(2),
                                                JEQ32(0, ALLOW),
                                                JEQ32(GRND_NONBLOCK, ALLOW),
                                            });
}

PolicyBuilder& PolicyBuilder::AllowLogForwarding() {
  AllowWrite();
  AllowSystemMalloc();
  AllowTcMalloc();

  AllowSyscalls({// from logging code
                 __NR_clock_gettime,
                 // From comms
                 __NR_gettid, __NR_close});

  // For LOG(FATAL)
  return AddPolicyOnSyscall(__NR_kill,
                            [](bpf_labels& labels) -> std::vector<sock_filter> {
                              return {
                                  ARG_32(0),
                                  JNE32(0, JUMP(&labels, pid_not_null)),
                                  ARG_32(1),
                                  JEQ32(SIGABRT, ALLOW),
                                  LABEL(&labels, pid_not_null),
                              };
                            });
}

PolicyBuilder& PolicyBuilder::AllowFutexOp(int op) {
  return AddPolicyOnSyscall(
      __NR_futex, {
                      ARG_32(1),
                      // a <- a & FUTEX_CMD_MASK
                      BPF_STMT(BPF_ALU + BPF_AND + BPF_K,
                               static_cast<uint32_t>(FUTEX_CMD_MASK)),
                      JEQ32(static_cast<uint32_t>(op) & FUTEX_CMD_MASK, ALLOW),
                  });
}

PolicyBuilder& PolicyBuilder::AllowStaticStartup() {
  AllowGetRlimit();
  AllowSyscalls({
      // These syscalls take a pointer, so no restriction.
      __NR_uname,
      __NR_brk,
      __NR_set_tid_address,

      // This syscall takes a pointer and a length.
      // We could restrict length, but it might change, so not worth it.
      __NR_set_robust_list,
  });

  AllowFutexOp(FUTEX_WAIT_BITSET);

  AddPolicyOnSyscall(__NR_rt_sigaction,
                     {
                         ARG_32(0),
                         // This is real-time signals used internally by libc.
                         JEQ32(__SIGRTMIN + 0, ALLOW),
                         JEQ32(__SIGRTMIN + 1, ALLOW),
                     });

  AddPolicyOnSyscall(__NR_rt_sigprocmask, {
                                              ARG_32(0),
                                              JEQ32(SIG_UNBLOCK, ALLOW),
                                          });

#if defined(__x86_64__)
  // The second argument is a pointer.
  AddPolicyOnSyscall(__NR_arch_prctl, {
                                          ARG_32(0),
                                          JEQ32(ARCH_SET_FS, ALLOW),
                                      });
#endif

  BlockSyscallWithErrno(__NR_readlink, ENOENT);

  return *this;
}

PolicyBuilder& PolicyBuilder::AllowDynamicStartup() {
  AllowRead();
  AllowStat();
  AllowSyscalls({__NR_lseek, __NR_close, __NR_munmap});
  AddPolicyOnSyscall(__NR_mprotect, {
                                        ARG_32(2),
                                        JEQ32(PROT_READ, ALLOW),
                                        JEQ32(PROT_NONE, ALLOW),
                                        JEQ32(PROT_READ | PROT_WRITE, ALLOW),
                                        JEQ32(PROT_READ | PROT_EXEC, ALLOW),
                                    });
  AllowStaticStartup();

  return AddPolicyOnMmap([](bpf_labels& labels) -> std::vector<sock_filter> {
    return {
        ARG_32(2),  // prot
        JEQ32(PROT_READ | PROT_EXEC, JUMP(&labels, prot_exec)),
        JEQ32(PROT_READ | PROT_WRITE, JUMP(&labels, prot_read_write)),
        JNE32(PROT_READ, JUMP(&labels, mmap_end)),

        // PROT_READ
        ARG_32(3),  // flags
        JEQ32(MAP_PRIVATE, ALLOW),
        JUMP(&labels, mmap_end),

        // PROT_READ | PROT_WRITE
        LABEL(&labels, prot_read_write),
        ARG_32(3),  // flags
        JEQ32(MAP_FILE | MAP_PRIVATE | MAP_FIXED | MAP_DENYWRITE, ALLOW),
        JEQ32(MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, ALLOW),
        JEQ32(MAP_ANONYMOUS | MAP_PRIVATE, ALLOW),
        JUMP(&labels, mmap_end),

        // PROT_READ | PROT_EXEC
        LABEL(&labels, prot_exec),
        ARG_32(3),  // flags
        JEQ32(MAP_FILE | MAP_PRIVATE | MAP_DENYWRITE, ALLOW),

        LABEL(&labels, mmap_end),
    };
  });
}

PolicyBuilder& PolicyBuilder::AddPolicyOnSyscall(unsigned int num,
                                                 BpfInitializer policy) {
  return AddPolicyOnSyscalls({num}, policy);
}

PolicyBuilder& PolicyBuilder::AddPolicyOnSyscall(
    unsigned int num, const std::vector<sock_filter>& policy) {
  return AddPolicyOnSyscalls({num}, policy);
}

PolicyBuilder& PolicyBuilder::AddPolicyOnSyscall(unsigned int num, BpfFunc f) {
  return AddPolicyOnSyscalls({num}, f);
}

PolicyBuilder& PolicyBuilder::AddPolicyOnSyscalls(
    SyscallInitializer nums, const std::vector<sock_filter>& policy) {
  auto resolved_policy = ResolveBpfFunc([nums, policy](bpf_labels& labels)
                                            -> std::vector<sock_filter> {
    std::vector<sock_filter> out;
    out.reserve(nums.size() + policy.size());
    for (auto num : nums) {
      out.insert(out.end(), {SYSCALL(num, JUMP(&labels, do_policy_l))});
    }
    out.insert(out.end(),
               {JUMP(&labels, dont_do_policy_l), LABEL(&labels, do_policy_l)});
    for (const auto& filter : policy) {
      // Syscall arch is expected as TRACE value
      if (filter.code == (BPF_RET | BPF_K) &&
          (filter.k & SECCOMP_RET_ACTION) == SECCOMP_RET_TRACE &&
          (filter.k & SECCOMP_RET_DATA) != Syscall::GetHostArch()) {
        LOG(WARNING) << "SANDBOX2_TRACE should be used in policy instead of "
                        "TRACE(value)";
        out.push_back(SANDBOX2_TRACE);
      } else {
        out.push_back(filter);
      }
    }
    out.push_back(LOAD_SYSCALL_NR);
    out.insert(out.end(), {LABEL(&labels, dont_do_policy_l)});
    return out;
  });
  // Pre-/Postcondition: Syscall number loaded into A register
  output_->user_policy_.insert(output_->user_policy_.end(),
                               resolved_policy.begin(), resolved_policy.end());
  return *this;
}

PolicyBuilder& PolicyBuilder::AddPolicyOnSyscalls(SyscallInitializer nums,
                                                  BpfInitializer policy) {
  std::vector<sock_filter> policy_vector(policy);
  return AddPolicyOnSyscalls(nums, policy_vector);
}

PolicyBuilder& PolicyBuilder::AddPolicyOnSyscalls(SyscallInitializer nums,
                                                  BpfFunc f) {
  return AddPolicyOnSyscalls(nums, ResolveBpfFunc(f));
}

PolicyBuilder& PolicyBuilder::AddPolicyOnMmap(BpfInitializer policy) {
#ifdef __NR_mmap2
  return AddPolicyOnSyscall(__NR_mmap2, policy);
#else
  return AddPolicyOnSyscall(__NR_mmap, policy);
#endif
}

PolicyBuilder& PolicyBuilder::AddPolicyOnMmap(
    const std::vector<sock_filter>& policy) {
#ifdef __NR_mmap2
  return AddPolicyOnSyscall(__NR_mmap2, policy);
#else
  return AddPolicyOnSyscall(__NR_mmap, policy);
#endif
}

PolicyBuilder& PolicyBuilder::AddPolicyOnMmap(BpfFunc f) {
#ifdef __NR_mmap2
  return AddPolicyOnSyscall(__NR_mmap2, f);
#else
  return AddPolicyOnSyscall(__NR_mmap, f);
#endif
}

PolicyBuilder& PolicyBuilder::DangerDefaultAllowAll() {
  output_->user_policy_.push_back(ALLOW);
  return *this;
}

::sapi::StatusOr<std::string> PolicyBuilder::ValidateAbsolutePath(
    absl::string_view path) {
  if (!file::IsAbsolutePath(path)) {
    return ::sapi::InvalidArgumentError(
        absl::StrCat("Path is not absolute: '", path, "'"));
  }
  return ValidatePath(path);
}

::sapi::StatusOr<std::string> PolicyBuilder::ValidatePath(
    absl::string_view path) {
  std::string fixed_path = file::CleanPath(path);
  if (fixed_path != path) {
    return ::sapi::InvalidArgumentError(absl::StrCat(
        "Path was not normalized. '", path, "' != '", fixed_path, "'"));
  }
  return fixed_path;
}

std::vector<sock_filter> PolicyBuilder::ResolveBpfFunc(BpfFunc f) {
  bpf_labels l = {0};

  std::vector<sock_filter> policy = f(l);
  if (bpf_resolve_jumps(&l, policy.data(), policy.size()) != 0) {
    SetError(::sapi::InternalError("Cannot resolve bpf jumps"));
  }

  return policy;
}

::sapi::StatusOr<std::unique_ptr<Policy>> PolicyBuilder::TryBuild() {
  if (!last_status_.ok()) {
    return last_status_;
  }

  if (!output_) {
    return ::sapi::FailedPreconditionError("Can only build policy once.");
  }

  if (use_namespaces_) {
    if (allow_unrestricted_networking_ && hostname_ != kDefaultHostname) {
      return ::sapi::FailedPreconditionError(
          "Cannot set hostname without network namespaces.");
    }
    output_->SetNamespace(absl::make_unique<Namespace>(
        allow_unrestricted_networking_, std::move(mounts_), hostname_));
  } else {
    // Not explicitly disabling them here as this is a technical limitation in
    // our stack trace collection functionality.
    LOG(WARNING) << "Using policy without namespaces, disabling stack traces on"
                 << " crash";
  }

  output_->collect_stacktrace_on_signal_ = collect_stacktrace_on_signal_;
  output_->collect_stacktrace_on_violation_ = collect_stacktrace_on_violation_;
  output_->collect_stacktrace_on_timeout_ = collect_stacktrace_on_timeout_;
  output_->collect_stacktrace_on_kill_ = collect_stacktrace_on_kill_;

  auto pb_description = absl::make_unique<PolicyBuilderDescription>();

  StoreDescription(pb_description.get());
  output_->policy_builder_description_ = std::move(pb_description);
  return std::move(output_);
}

PolicyBuilder& PolicyBuilder::AddFile(absl::string_view path, bool is_ro) {
  return AddFileAt(path, path, is_ro);
}

PolicyBuilder& PolicyBuilder::SetError(const ::sapi::Status& status) {
  LOG(ERROR) << status;
  last_status_ = status;
  return *this;
}

PolicyBuilder& PolicyBuilder::AddFileAt(absl::string_view outside,
                                        absl::string_view inside, bool is_ro) {
  EnableNamespaces();

  auto fixed_outside_or = ValidateAbsolutePath(outside);
  if (!fixed_outside_or.ok()) {
    SetError(fixed_outside_or.status());
    return *this;
  }
  auto fixed_outside = std::move(fixed_outside_or.ValueOrDie());

  if (absl::StartsWith(fixed_outside, "/proc/self")) {
    SetError(::sapi::InvalidArgumentError(
        absl::StrCat("Cannot add /proc/self mounts, you need to mount the "
                     "whole /proc instead. You tried to mount ",
                     outside)));
    return *this;
  }

  auto status = mounts_.AddFileAt(fixed_outside, inside, is_ro);
  if (!status.ok()) {
    SetError(::sapi::InternalError(absl::StrCat("Could not add file ", outside,
                                                " => ", inside, ": ",
                                                status.message())));
  }

  return *this;
}

PolicyBuilder& PolicyBuilder::AddLibrariesForBinary(
    absl::string_view path, absl::string_view ld_library_path) {
  EnableNamespaces();

  auto fixed_path_or = ValidatePath(path);
  if (!fixed_path_or.ok()) {
    SetError(fixed_path_or.status());
    return *this;
  }
  auto fixed_path = std::move(fixed_path_or.ValueOrDie());

  auto status = mounts_.AddMappingsForBinary(fixed_path, ld_library_path);
  if (!status.ok()) {
    SetError(::sapi::InternalError(absl::StrCat(
        "Could not add libraries for ", fixed_path, ": ", status.message())));
  }
  return *this;
}

PolicyBuilder& PolicyBuilder::AddLibrariesForBinary(
    int fd, absl::string_view ld_library_path) {
  return AddLibrariesForBinary(absl::StrCat("/proc/self/fd/", fd),
                               ld_library_path);
}

PolicyBuilder& PolicyBuilder::AddDirectory(absl::string_view path, bool is_ro) {
  return AddDirectoryAt(path, path, is_ro);
}

PolicyBuilder& PolicyBuilder::AddDirectoryAt(absl::string_view outside,
                                             absl::string_view inside,
                                             bool is_ro) {
  EnableNamespaces();

  auto fixed_outside_or = ValidateAbsolutePath(outside);
  if (!fixed_outside_or.ok()) {
    SetError(fixed_outside_or.status());
    return *this;
  }
  auto fixed_outside = std::move(fixed_outside_or.ValueOrDie());
  if (absl::StartsWith(fixed_outside, "/proc/self")) {
    SetError(::sapi::InvalidArgumentError(
        absl::StrCat("Cannot add /proc/self mounts, you need to mount the "
                     "whole /proc instead. You tried to mount ",
                     outside)));
    return *this;
  }

  auto status = mounts_.AddDirectoryAt(fixed_outside, inside, is_ro);
  if (!status.ok()) {
    SetError(::sapi::InternalError(absl::StrCat("Could not add directory ",
                                                outside, " => ", inside, ": ",
                                                status.message())));
  }

  return *this;
}

PolicyBuilder& PolicyBuilder::AddTmpfs(absl::string_view inside, size_t sz) {
  EnableNamespaces();

  auto status = mounts_.AddTmpfs(inside, sz);
  if (!status.ok()) {
    SetError(::sapi::InternalError(absl::StrCat(
        "Could not mount tmpfs ", inside, ": ", status.message())));
  }

  return *this;
}

PolicyBuilder& PolicyBuilder::AllowUnrestrictedNetworking() {
  EnableNamespaces();
  allow_unrestricted_networking_ = true;

  return *this;
}

PolicyBuilder& PolicyBuilder::SetHostname(absl::string_view hostname) {
  EnableNamespaces();
  hostname_ = std::string(hostname);

  return *this;
}

PolicyBuilder& PolicyBuilder::CollectStacktracesOnViolation(bool enable) {
  collect_stacktrace_on_violation_ = enable;
  return *this;
}

PolicyBuilder& PolicyBuilder::CollectStacktracesOnSignal(bool enable) {
  collect_stacktrace_on_signal_ = enable;
  return *this;
}

PolicyBuilder& PolicyBuilder::CollectStacktracesOnTimeout(bool enable) {
  collect_stacktrace_on_timeout_ = enable;
  return *this;
}

PolicyBuilder& PolicyBuilder::CollectStacktracesOnKill(bool enable) {
  collect_stacktrace_on_kill_ = enable;
  return *this;
}

void PolicyBuilder::StoreDescription(PolicyBuilderDescription* pb_description) {
  for (const auto& handled_syscall : handled_syscalls_) {
    pb_description->add_handled_syscalls(handled_syscall);
  }
}

}  // namespace sandbox2
