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

#include "sandboxed_api/sandbox2/policybuilder.h"

#include <asm/ioctls.h>  // For TCGETS
#include <fcntl.h>       // For the fcntl flags
#include <linux/filter.h>
#include <linux/futex.h>
#include <linux/net.h>     // For SYS_CONNECT
#include <linux/random.h>  // For GRND_NONBLOCK
#include <sys/mman.h>      // For mmap arguments
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <syscall.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <cstdint>
#include <deque>
#include <memory>
#include <utility>

#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/namespace.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_macros.h"

#if defined(SAPI_X86_64)
#include <asm/prctl.h>
#elif defined(SAPI_PPC64_LE)
#include <asm/termbits.h>  // On PPC, TCGETS macro needs termios
#endif

namespace sandbox2 {
namespace {

namespace file = ::sapi::file;

constexpr std::array<uint32_t, 2> kMmapSyscalls = {
#ifdef __NR_mmap2
    __NR_mmap2,
#endif
#ifdef __NR_mmap
    __NR_mmap,
#endif
};

bool CheckBpfBounds(const sock_filter& filter, size_t max_jmp) {
  if (BPF_CLASS(filter.code) == BPF_JMP) {
    if (BPF_OP(filter.code) == BPF_JA) {
      return filter.k <= max_jmp;
    }
    return filter.jt <= max_jmp && filter.jf <= max_jmp;
  }
  return true;
}

bool IsOnReadOnlyDev(const std::string& path) {
  struct statvfs vfs;
  if (TEMP_FAILURE_RETRY(statvfs(path.c_str(), &vfs)) == -1) {
    PLOG(ERROR) << "Could not statvfs: " << path.c_str();
    return false;
  }
  return vfs.f_flag & ST_RDONLY;
}

}  // namespace

PolicyBuilder& PolicyBuilder::AllowSyscall(uint32_t num) {
  if (handled_syscalls_.insert(num).second) {
    user_policy_.insert(user_policy_.end(), {SYSCALL(num, ALLOW)});
  }
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowSyscalls(absl::Span<const uint32_t> nums) {
  for (auto num : nums) {
    AllowSyscall(num);
  }
  return *this;
}

PolicyBuilder& PolicyBuilder::BlockSyscallsWithErrno(
    absl::Span<const uint32_t> nums, int error) {
  for (auto num : nums) {
    BlockSyscallWithErrno(num, error);
  }
  return *this;
}

PolicyBuilder& PolicyBuilder::BlockSyscallWithErrno(uint32_t num, int error) {
  if (handled_syscalls_.insert(num).second) {
    user_policy_.insert(user_policy_.end(), {SYSCALL(num, ERRNO(error))});
    if (num == __NR_bpf) {
      user_policy_handles_bpf_ = true;
    }
    if (num == __NR_ptrace) {
      user_policy_handles_ptrace_ = true;
    }
  }
  return *this;
}

PolicyBuilder& PolicyBuilder::OverridableBlockSyscallWithErrno(uint32_t num,
                                                               int error) {
  overridable_policy_.insert(overridable_policy_.end(),
                             {SYSCALL(num, ERRNO(error))});
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowEpoll() {
  return AllowSyscalls({
#ifdef __NR_epoll_create
      __NR_epoll_create,
#endif
#ifdef __NR_epoll_create1
      __NR_epoll_create1,
#endif
#ifdef __NR_epoll_ctl
      __NR_epoll_ctl,
#endif
#ifdef __NR_epoll_wait
      __NR_epoll_wait,
#endif
#ifdef __NR_epoll_pwait
      __NR_epoll_pwait,
#endif
#ifdef __NR_epoll_pwait2
      __NR_epoll_pwait2,
#endif
  });
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
  AllowWipeOnFork();

  return AddPolicyOnMmap([](bpf_labels& labels) -> std::vector<sock_filter> {
    return {
        ARG_32(2),  // prot
        JEQ32(PROT_NONE, JUMP(&labels, prot_none)),
        JNE32(PROT_READ | PROT_WRITE, JUMP(&labels, mmap_end)),

        // PROT_READ | PROT_WRITE
        ARG_32(3),  // flags
        BPF_STMT(BPF_ALU | BPF_AND | BPF_K,
                 ~uint32_t{MAP_FIXED | MAP_NORESERVE}),
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
  AllowRestartableSequences(kRequireFastFences);
  AllowSyscalls(
      {__NR_munmap, __NR_nanosleep, __NR_brk, __NR_mincore, __NR_membarrier});
  AllowLimitedMadvise();

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
  AllowFutexOp(FUTEX_WAKE);
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

PolicyBuilder& PolicyBuilder::AllowLlvmSanitizers() {
  if constexpr (sapi::sanitizers::IsAny()) {
    // *san use a custom allocator that runs mmap/unmap under the hood.  For
    // example:
    // https://github.com/llvm/llvm-project/blob/596d534ac3524052df210be8d3c01a33b2260a42/compiler-rt/lib/asan/asan_allocator.cpp#L980
    // https://github.com/llvm/llvm-project/blob/62ec4ac90738a5f2d209ed28c822223e58aaaeb7/compiler-rt/lib/sanitizer_common/sanitizer_allocator_secondary.h#L98
    AllowMmap();
    AllowSyscall(__NR_munmap);

    // https://github.com/llvm/llvm-project/blob/4bbc3290a25c0dc26007912a96e0f77b2092ee56/compiler-rt/lib/sanitizer_common/sanitizer_stack_store.cpp#L293
    AddPolicyOnSyscall(__NR_mprotect,
                       {
                           ARG_32(2),
                           BPF_STMT(BPF_AND | BPF_ALU | BPF_K,
                                    ~uint32_t{PROT_READ | PROT_WRITE}),
                           JEQ32(PROT_NONE, ALLOW),
                       });

    AddPolicyOnSyscall(__NR_madvise, {
                                         ARG_32(2),
                                         JEQ32(MADV_DONTDUMP, ALLOW),
                                         JEQ32(MADV_NOHUGEPAGE, ALLOW),
                                     });
    // Sanitizers read from /proc. For example:
    // https://github.com/llvm/llvm-project/blob/634da7a1c61ee8c173e90a841eb1f4ea03caa20b/compiler-rt/lib/sanitizer_common/sanitizer_linux.cpp#L1155
    AddDirectory("/proc");
    // Sanitizers need pid for reports. For example:
    // https://github.com/llvm/llvm-project/blob/634da7a1c61ee8c173e90a841eb1f4ea03caa20b/compiler-rt/lib/sanitizer_common/sanitizer_linux.cpp#L740
    AllowGetPIDs();
    // Sanitizers may try color output. For example:
    // https://github.com/llvm/llvm-project/blob/87dd3d350c4ce0115b2cdf91d85ddd05ae2661aa/compiler-rt/lib/sanitizer_common/sanitizer_posix_libcdep.cpp#L157
    OverridableBlockSyscallWithErrno(__NR_ioctl, EPERM);
    // https://github.com/llvm/llvm-project/blob/02c2b472b510ff55679844c087b66e7837e13dc2/compiler-rt/lib/sanitizer_common/sanitizer_linux.cpp#L434
#ifdef __NR_readlink
    OverridableBlockSyscallWithErrno(__NR_readlink, ENOENT);
#endif
    OverridableBlockSyscallWithErrno(__NR_readlinkat, ENOENT);
  }
  if constexpr (sapi::sanitizers::IsASan()) {
    AllowSyscall(__NR_sigaltstack);
  }
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
  return AllowSyscalls(kMmapSyscalls);
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

PolicyBuilder& PolicyBuilder::AllowAccess() {
#ifdef __NR_access
  AllowSyscall(__NR_access);
#endif
#ifdef __NR_faccessat
  AllowSyscall(__NR_faccessat);
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
#ifdef __NR_sigaltstack
      __NR_sigaltstack,
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

PolicyBuilder& PolicyBuilder::AllowRestartableSequencesWithProcFiles(
    CpuFenceMode cpu_fence_mode) {
  AllowRestartableSequences(cpu_fence_mode);
  AddFile("/proc/cpuinfo");
  AddFile("/proc/stat");
  AddDirectory("/sys/devices/system/cpu");
  if (cpu_fence_mode == kAllowSlowFences) {
    AddFile("/proc/self/cpuset");
  }
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowRestartableSequences(
    CpuFenceMode cpu_fence_mode) {
#ifdef __NR_rseq
  AllowSyscall(__NR_rseq);
#endif
  AddPolicyOnMmap([](bpf_labels& labels) -> std::vector<sock_filter> {
    return {
        ARG_32(2),  // prot
        JNE32(PROT_READ | PROT_WRITE, JUMP(&labels, mmap_end)),

        ARG_32(3),  // flags
        JNE32(MAP_PRIVATE | MAP_ANONYMOUS, JUMP(&labels, mmap_end)),

        ALLOW,
        LABEL(&labels, mmap_end),
    };
  });
  AllowSyscall(__NR_getcpu);
  AllowSyscall(__NR_membarrier);
  AllowFutexOp(FUTEX_WAIT);
  AllowFutexOp(FUTEX_WAKE);
  AddPolicyOnSyscall(__NR_rt_sigprocmask, {
                                              ARG_32(0),
                                              JEQ32(SIG_SETMASK, ALLOW),
                                          });
  if (cpu_fence_mode == kAllowSlowFences) {
    AllowSyscall(__NR_sched_getaffinity);
    AllowSyscall(__NR_sched_setaffinity);
  }
  return *this;
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
#ifdef __NR_getrlimit
      __NR_getrlimit,
#endif
#ifdef __NR_ugetrlimit
      __NR_ugetrlimit,
#endif
  });
}

PolicyBuilder& PolicyBuilder::AllowSetRlimit() {
  return AllowSyscalls({
#ifdef __NR_setrlimit
      __NR_setrlimit,
#endif
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

PolicyBuilder& PolicyBuilder::AllowWipeOnFork() {
  // System headers may not be recent enough to include MADV_WIPEONFORK.
  static constexpr uint32_t kMadv_WipeOnFork = 18;
  // The -1 value is used by code to probe that the kernel returns -EINVAL for
  // unknown values because some environments, like qemu, ignore madvise
  // completely, but code needs to know whether WIPEONFORK took effect.
  return AddPolicyOnSyscall(__NR_madvise,
                            {
                                ARG_32(2),
                                JEQ32(kMadv_WipeOnFork, ALLOW),
                                JEQ32(static_cast<uint32_t>(-1), ALLOW),
                            });
}

PolicyBuilder& PolicyBuilder::AllowLogForwarding() {
  AllowWrite();
  AllowSystemMalloc();
  AllowTcMalloc();

  // From comms
  AllowGetPIDs();
  AllowSyscalls({// from logging code
                 __NR_clock_gettime,
                 // From comms
                 __NR_gettid, __NR_close});

  // For generating stacktraces in logging (e.g. `LOG(FATAL)`)
  AddPolicyOnSyscall(__NR_rt_sigprocmask, {
                                              ARG_32(0),
                                              JEQ32(SIG_BLOCK, ALLOW),
                                          });
  AllowSyscall(__NR_prlimit64);

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

PolicyBuilder& PolicyBuilder::AllowUnlink() {
  AllowSyscalls({
#ifdef __NR_unlink
      __NR_unlink,
#endif
      __NR_unlinkat,
  });
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowRename() {
  AllowSyscalls({
#ifdef __NR_rename
      __NR_rename,
#endif
      __NR_renameat,
#ifdef __NR_renameat2
      __NR_renameat2,
#endif
  });
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowPrctlSetName() {
  AddPolicyOnSyscall(__NR_prctl, {ARG_32(0), JEQ32(PR_SET_NAME, ALLOW)});
  return *this;
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
    __NR_uname, __NR_brk, __NR_set_tid_address,

#if defined(__ARM_NR_set_tls)
        // libc sets the TLS during startup
        __ARM_NR_set_tls,
#endif

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

  AllowSyscall(__NR_rt_sigprocmask);

#ifdef SAPI_X86_64
  // The second argument is a pointer.
  AddPolicyOnSyscall(__NR_arch_prctl, {
                                          ARG_32(0),
                                          JEQ32(ARCH_SET_FS, ALLOW),
                                      });
#endif

  if constexpr (sapi::host_cpu::IsArm64()) {
    OverridableBlockSyscallWithErrno(__NR_readlinkat, ENOENT);
  }
#ifdef __NR_readlink
  OverridableBlockSyscallWithErrno(__NR_readlink, ENOENT);
#endif

  AddPolicyOnSyscall(__NR_mprotect, {
                                        ARG_32(2),
                                        JEQ32(PROT_READ, ALLOW),
                                    });

  return *this;
}

PolicyBuilder& PolicyBuilder::AllowDynamicStartup() {
#ifdef __ANDROID__
  AllowAccess();
  AllowSafeFcntl();
  AllowGetIDs();
  AllowGetPIDs();
  AllowGetRandom();
  AllowOpen();
  AllowSyscalls({
#ifdef __NR_fstatfs
      __NR_fstatfs,
#endif
#ifdef __NR_fstatfs64
      __NR_fstatfs64,
#endif
      __NR_readlinkat,
      __NR_sched_getaffinity,
      __NR_sched_getscheduler,
  });
  AllowHandleSignals();
  AllowFutexOp(FUTEX_WAKE_PRIVATE);
  AddPolicyOnSyscall(__NR_prctl,
                     [](bpf_labels& labels) -> std::vector<sock_filter> {
                       return {
                           ARG_32(0),  // option
                           JEQ32(PR_GET_DUMPABLE, ALLOW),
                           JNE32(PR_SET_VMA, JUMP(&labels, prctl_end)),

                           ARG_32(1),  // arg2
                           JEQ32(PR_SET_VMA_ANON_NAME, ALLOW),

                           LABEL(&labels, prctl_end),
                       };
                     });
  AddPolicyOnSyscall(__NR_mremap,
                     {
                         ARG_32(3),
                         JEQ32(MREMAP_MAYMOVE | MREMAP_FIXED, ALLOW),
                     });
  AddPolicyOnMmap([](bpf_labels& labels) -> std::vector<sock_filter> {
    return {
        ARG_32(2),  // prot
        JEQ32(PROT_NONE, JUMP(&labels, prot_none)),
        JEQ32(PROT_READ, JUMP(&labels, prot_read)),
        JEQ32(PROT_READ | PROT_WRITE, JUMP(&labels, prot_RW_or_RX)),
        JEQ32(PROT_READ | PROT_EXEC, JUMP(&labels, prot_RW_or_RX)),

        // PROT_NONE
        LABEL(&labels, prot_none),
        ARG_32(3),  // flags
        JEQ32(MAP_PRIVATE | MAP_ANONYMOUS, ALLOW),
        JEQ32(MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, ALLOW),
        JUMP(&labels, mmap_end),

        // PROT_READ
        LABEL(&labels, prot_read),
        ARG_32(3),  // flags
        JEQ32(MAP_SHARED, ALLOW),
        JEQ32(MAP_PRIVATE, ALLOW),
        JEQ32(MAP_PRIVATE | MAP_FIXED, ALLOW),
        JUMP(&labels, mmap_end),

        // PROT_READ | PROT_WRITE
        // PROT_READ | PROT_EXEC
        LABEL(&labels, prot_RW_or_RX),
        ARG_32(3),  // flags
        JEQ32(MAP_PRIVATE | MAP_FIXED, ALLOW),

        LABEL(&labels, mmap_end),
    };
  });
#endif

  AllowRead();
  AllowStat();
  AllowSyscalls({__NR_lseek,
#ifdef __NR__llseek
                 __NR__llseek,  // Newer glibc on PPC
#endif
                 __NR_close, __NR_munmap});
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

PolicyBuilder& PolicyBuilder::AddPolicyOnSyscall(
    uint32_t num, absl::Span<const sock_filter> policy) {
  return AddPolicyOnSyscalls({num}, policy);
}

PolicyBuilder& PolicyBuilder::AddPolicyOnSyscall(uint32_t num, BpfFunc f) {
  return AddPolicyOnSyscalls({num}, f);
}

PolicyBuilder& PolicyBuilder::AddPolicyOnSyscalls(
    absl::Span<const uint32_t> nums, absl::Span<const sock_filter> policy) {
  std::deque<sock_filter> out;
  // Insert and verify the policy.
  out.insert(out.end(), policy.begin(), policy.end());
  for (size_t i = 0; i < out.size(); ++i) {
    sock_filter& filter = out[i];
    const size_t max_jump = out.size() - i - 1;
    if (!CheckBpfBounds(filter, max_jump)) {
      SetError(absl::InvalidArgumentError("bpf jump out of bounds"));
      return *this;
    }
    // Syscall arch is expected as TRACE value
    if (filter.code == (BPF_RET | BPF_K) &&
        (filter.k & SECCOMP_RET_ACTION) == SECCOMP_RET_TRACE &&
        (filter.k & SECCOMP_RET_DATA) != Syscall::GetHostArch()) {
      LOG(WARNING) << "SANDBOX2_TRACE should be used in policy instead of "
                      "TRACE(value)";
      filter = SANDBOX2_TRACE;
    }
  }
  // Pre-/Postcondition: Syscall number loaded into A register
  out.push_back(LOAD_SYSCALL_NR);
  if (out.size() > std::numeric_limits<uint32_t>::max()) {
    SetError(absl::InvalidArgumentError("syscall policy is too long"));
    return *this;
  }
  // Create jumps for each syscall.
  size_t do_policy_loc = out.size();
  // Iterate in reverse order and prepend instruction, so that jumps can be
  // calculated easily.
  constexpr size_t kMaxShortJump = 255;
  bool last = true;
  for (auto it = std::rbegin(nums); it != std::rend(nums); ++it) {
    // If syscall is not matched try with the next one.
    uint8_t jf = 0;
    // If last syscall on the list does not match skip the policy by jumping
    // over it.
    if (last) {
      if (out.size() > kMaxShortJump) {
        out.push_front(
            BPF_STMT(BPF_JMP + BPF_JA, static_cast<uint32_t>(out.size())));
      } else {
        jf = out.size();
      }
      last = false;
    }
    // Add a helper absolute jump if needed - the policy/last helper jump is
    // out of reach of a short jump.
    if ((out.size() - do_policy_loc) > kMaxShortJump) {
      out.push_front(BPF_STMT(
          BPF_JMP + BPF_JA, static_cast<uint32_t>(out.size() - policy.size())));
      do_policy_loc = out.size();
      ++jf;
    }
    uint8_t jt = out.size() - do_policy_loc;
    out.push_front(BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, *it, jt, jf));
  }
  user_policy_.insert(user_policy_.end(), out.begin(), out.end());
  return *this;
}

PolicyBuilder& PolicyBuilder::AddPolicyOnSyscalls(
    absl::Span<const uint32_t> nums, BpfFunc f) {
  return AddPolicyOnSyscalls(nums, ResolveBpfFunc(f));
}

PolicyBuilder& PolicyBuilder::AddPolicyOnMmap(
    absl::Span<const sock_filter> policy) {
  return AddPolicyOnSyscalls(kMmapSyscalls, policy);
}

PolicyBuilder& PolicyBuilder::AddPolicyOnMmap(BpfFunc f) {
  return AddPolicyOnSyscalls(kMmapSyscalls, f);
}

PolicyBuilder& PolicyBuilder::DangerDefaultAllowAll() {
  user_policy_.push_back(ALLOW);
  return *this;
}

absl::StatusOr<std::string> PolicyBuilder::ValidateAbsolutePath(
    absl::string_view path) {
  if (!file::IsAbsolutePath(path)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Path is not absolute: '", path, "'"));
  }
  return ValidatePath(path);
}

absl::StatusOr<std::string> PolicyBuilder::ValidatePath(
    absl::string_view path) {
  std::string fixed_path = file::CleanPath(path);
  if (fixed_path != path) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Path was not normalized. '", path, "' != '", fixed_path, "'"));
  }
  return fixed_path;
}

std::vector<sock_filter> PolicyBuilder::ResolveBpfFunc(BpfFunc f) {
  bpf_labels l = {0};

  std::vector<sock_filter> policy = f(l);
  if (bpf_resolve_jumps(&l, policy.data(), policy.size()) != 0) {
    SetError(absl::InternalError("Cannot resolve bpf jumps"));
  }

  return policy;
}

absl::StatusOr<std::unique_ptr<Policy>> PolicyBuilder::TryBuild() {
  // Using `new` to access a non-public constructor.
  auto output = absl::WrapUnique(new Policy());

  if (user_policy_.size() > kMaxUserPolicyLength) {
    return absl::FailedPreconditionError(
        absl::StrCat("User syscall policy is to long (", user_policy_.size(),
                     " > ", kMaxUserPolicyLength, ")."));
  }

  if (!last_status_.ok()) {
    return last_status_;
  }

  if (already_built_) {
    return absl::FailedPreconditionError("Can only build policy once.");
  }

  if (use_namespaces_) {
    if (allow_unrestricted_networking_ && hostname_ != kDefaultHostname) {
      return absl::FailedPreconditionError(
          "Cannot set hostname without network namespaces.");
    }
    output->SetNamespace(std::make_unique<Namespace>(
        allow_unrestricted_networking_, std::move(mounts_), hostname_,
        allow_mount_propagation_));
  } else {
    // Not explicitly disabling them here as this is a technical limitation in
    // our stack trace collection functionality.
    LOG(WARNING) << "Using policy without namespaces, disabling stack traces on"
                 << " crash";
  }

  output->collect_stacktrace_on_signal_ = collect_stacktrace_on_signal_;
  output->collect_stacktrace_on_violation_ = collect_stacktrace_on_violation_;
  output->collect_stacktrace_on_timeout_ = collect_stacktrace_on_timeout_;
  output->collect_stacktrace_on_kill_ = collect_stacktrace_on_kill_;
  output->collect_stacktrace_on_exit_ = collect_stacktrace_on_exit_;
  output->user_policy_ = std::move(user_policy_);
  output->user_policy_.insert(output->user_policy_.end(),
                              overridable_policy_.begin(),
                              overridable_policy_.end());
  output->user_policy_handles_bpf_ = user_policy_handles_bpf_;
  output->user_policy_handles_ptrace_ = user_policy_handles_ptrace_;

  auto pb_description = std::make_unique<PolicyBuilderDescription>();

  StoreDescription(pb_description.get());
  output->policy_builder_description_ = std::move(pb_description);
  output->allowed_hosts_ = std::move(allowed_hosts_);
  already_built_ = true;
  return std::move(output);
}

PolicyBuilder& PolicyBuilder::AddFile(absl::string_view path, bool is_ro) {
  return AddFileAt(path, path, is_ro);
}

PolicyBuilder& PolicyBuilder::AddFileAt(absl::string_view outside,
                                        absl::string_view inside, bool is_ro) {
  EnableNamespaces();  // NOLINT(clang-diagnostic-deprecated-declarations)

  auto valid_outside = ValidateAbsolutePath(outside);
  if (!valid_outside.ok()) {
    SetError(valid_outside.status());
    return *this;
  }

  if (absl::StartsWith(*valid_outside, "/proc/self") &&
      *valid_outside != "/proc/self/cpuset") {
    SetError(absl::InvalidArgumentError(
        absl::StrCat("Cannot add /proc/self mounts, you need to mount the "
                     "whole /proc instead. You tried to mount ",
                     outside)));
    return *this;
  }

  if (!is_ro && IsOnReadOnlyDev(*valid_outside)) {
    SetError(absl::FailedPreconditionError(
        absl::StrCat("Cannot add ", outside,
                     " as read-write as it's on a read-only device")));
    return *this;
  }

  if (auto status = mounts_.AddFileAt(*valid_outside, inside, is_ro);
      !status.ok()) {
    SetError(
        absl::InternalError(absl::StrCat("Could not add file ", outside, " => ",
                                         inside, ": ", status.message())));
  }
  return *this;
}

PolicyBuilder& PolicyBuilder::AddLibrariesForBinary(
    absl::string_view path, absl::string_view ld_library_path) {
  EnableNamespaces();  // NOLINT(clang-diagnostic-deprecated-declarations)

  auto valid_path = ValidatePath(path);
  if (!valid_path.ok()) {
    SetError(valid_path.status());
    return *this;
  }

  if (auto status = mounts_.AddMappingsForBinary(*valid_path, ld_library_path);
      !status.ok()) {
    SetError(absl::InternalError(absl::StrCat(
        "Could not add libraries for ", *valid_path, ": ", status.message())));
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
  EnableNamespaces();  // NOLINT(clang-diagnostic-deprecated-declarations)

  auto valid_outside = ValidateAbsolutePath(outside);
  if (!valid_outside.ok()) {
    SetError(valid_outside.status());
    return *this;
  }

  if (absl::StartsWith(*valid_outside, "/proc/self")) {
    SetError(absl::InvalidArgumentError(
        absl::StrCat("Cannot add /proc/self mounts, you need to mount the "
                     "whole /proc instead. You tried to mount ",
                     outside)));
    return *this;
  }

  if (!is_ro && IsOnReadOnlyDev(*valid_outside)) {
    SetError(absl::FailedPreconditionError(
        absl::StrCat("Cannot add ", outside,
                     " as read-write as it's on a read-only device")));
    return *this;
  }

  if (absl::Status status =
          mounts_.AddDirectoryAt(*valid_outside, inside, is_ro);
      !status.ok()) {
    SetError(absl::InternalError(absl::StrCat("Could not add directory ",
                                              outside, " => ", inside, ": ",
                                              status.message())));
    return *this;
  }
  return *this;
}

PolicyBuilder& PolicyBuilder::AddTmpfs(absl::string_view inside, size_t size) {
  EnableNamespaces();  // NOLINT(clang-diagnostic-deprecated-declarations)

  if (auto status = mounts_.AddTmpfs(inside, size); !status.ok()) {
    SetError(absl::InternalError(absl::StrCat("Could not mount tmpfs ", inside,
                                              ": ", status.message())));
  }
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowUnrestrictedNetworking() {
  EnableNamespaces();  // NOLINT(clang-diagnostic-deprecated-declarations)
  allow_unrestricted_networking_ = true;

  return *this;
}

PolicyBuilder& PolicyBuilder::SetHostname(absl::string_view hostname) {
  EnableNamespaces();  // NOLINT(clang-diagnostic-deprecated-declarations)
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

PolicyBuilder& PolicyBuilder::CollectStacktracesOnExit(bool enable) {
  collect_stacktrace_on_exit_ = enable;
  return *this;
}

PolicyBuilder& PolicyBuilder::AddNetworkProxyPolicy() {
  if (allowed_hosts_) {
    SetError(absl::FailedPreconditionError(
        "AddNetworkProxyPolicy or AddNetworkProxyHandlerPolicy can be called "
        "at most once"));
    return *this;
  }

  allowed_hosts_ = AllowedHosts();

  AllowFutexOp(FUTEX_WAKE);
  AllowFutexOp(FUTEX_WAIT);
  AllowFutexOp(FUTEX_WAIT_BITSET);
  AllowSyscalls({
#ifdef __NR_dup2
      __NR_dup2,
#endif
      __NR_recvmsg,
      __NR_close,
      __NR_gettid,
  });
  AddPolicyOnSyscall(__NR_socket, {
                                      ARG_32(0),
                                      JEQ32(AF_INET, ALLOW),
                                      JEQ32(AF_INET6, ALLOW),
                                  });
  AddPolicyOnSyscall(__NR_getsockopt,
                     [](bpf_labels& labels) -> std::vector<sock_filter> {
                       return {
                           ARG_32(1),
                           JNE32(SOL_SOCKET, JUMP(&labels, getsockopt_end)),
                           ARG_32(2),
                           JEQ32(SO_TYPE, ALLOW),
                           LABEL(&labels, getsockopt_end),
                       };
                     });
#ifdef SAPI_PPC64_LE
  AddPolicyOnSyscall(__NR_socketcall, {
                                          ARG_32(0),
                                          JEQ32(SYS_SOCKET, ALLOW),
                                          JEQ32(SYS_GETSOCKOPT, ALLOW),
                                          JEQ32(SYS_RECVMSG, ALLOW),
                                      });
#endif
  return *this;
}

PolicyBuilder& PolicyBuilder::AddNetworkProxyHandlerPolicy() {
  AddNetworkProxyPolicy();
  AllowSyscall(__NR_rt_sigreturn);

  AddPolicyOnSyscall(__NR_rt_sigaction, {
                                            ARG_32(0),
                                            JEQ32(SIGSYS, ALLOW),
                                        });

  AddPolicyOnSyscall(__NR_rt_sigprocmask, {
                                              ARG_32(0),
                                              JEQ32(SIG_UNBLOCK, ALLOW),
                                          });

  AddPolicyOnSyscall(__NR_connect, {TRAP(0)});
#ifdef SAPI_PPC64_LE
  AddPolicyOnSyscall(__NR_socketcall, {
                                          ARG_32(0),
                                          JEQ32(SYS_CONNECT, TRAP(0)),
                                      });
#endif
  return *this;
}

PolicyBuilder& PolicyBuilder::TrapPtrace() {
  AddPolicyOnSyscall(__NR_ptrace, {TRAP(0)});
  user_policy_handles_ptrace_ = true;
  return *this;
}

PolicyBuilder& PolicyBuilder::SetRootWritable() {
  EnableNamespaces();  // NOLINT(clang-diagnostic-deprecated-declarations)
  mounts_.SetRootWritable();

  return *this;
}

void PolicyBuilder::StoreDescription(PolicyBuilderDescription* pb_description) {
  for (const auto& handled_syscall : handled_syscalls_) {
    pb_description->add_handled_syscalls(handled_syscall);
  }
}

PolicyBuilder& PolicyBuilder::AllowIPv4(const std::string& ip_and_mask,
                                        uint32_t port) {
  if (!allowed_hosts_) {
    SetError(absl::FailedPreconditionError(
        "AddNetworkProxyPolicy or AddNetworkProxyHandlerPolicy must be called "
        "before adding IP rules"));
    return *this;
  }

  absl::Status status = allowed_hosts_->AllowIPv4(ip_and_mask, port);
  if (!status.ok()) {
    SetError(status);
  }
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowIPv6(const std::string& ip_and_mask,
                                        uint32_t port) {
  if (!allowed_hosts_) {
    SetError(absl::FailedPreconditionError(
        "AddNetworkProxyPolicy or AddNetworkProxyHandlerPolicy must be called "
        "before adding IP rules"));
    return *this;
  }

  absl::Status status = allowed_hosts_->AllowIPv6(ip_and_mask, port);
  if (!status.ok()) {
    SetError(status);
  }
  return *this;
}

PolicyBuilder& PolicyBuilder::SetError(const absl::Status& status) {
  LOG(ERROR) << status;
  last_status_ = status;
  return *this;
}

}  // namespace sandbox2
