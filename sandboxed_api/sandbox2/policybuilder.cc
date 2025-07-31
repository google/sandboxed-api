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

#include <fcntl.h>  // For the fcntl flags
#include <linux/bpf_common.h>
#include <linux/filter.h>
#include <linux/futex.h>
#include <linux/random.h>  // For GRND_NONBLOCK
#include <linux/seccomp.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include <sys/mman.h>  // For mmap arguments
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <syscall.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/allowlists/all_syscalls.h"
#include "sandboxed_api/sandbox2/allowlists/map_exec.h"
#include "sandboxed_api/sandbox2/allowlists/mount_propagation.h"
#include "sandboxed_api/sandbox2/allowlists/namespaces.h"
#include "sandboxed_api/sandbox2/allowlists/seccomp_speculation.h"
#include "sandboxed_api/sandbox2/allowlists/trace_all_syscalls.h"
#include "sandboxed_api/sandbox2/allowlists/unrestricted_networking.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/sandbox2/namespace.h"
#include "sandboxed_api/sandbox2/network_proxy/filtering.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"

#if defined(SAPI_X86_64)
#include <asm/prctl.h>
#elif defined(SAPI_PPC64_LE)
#include <asm/termbits.h>  // On PPC, TCGETS macro needs termios
#endif

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000  // Linux 4.17+
#endif

#ifndef MADV_POPULATE_READ
#define MADV_POPULATE_READ 22  // Linux 5.14+
#endif
#ifndef MADV_POPULATE_WRITE  // Linux 5.14+
#define MADV_POPULATE_WRITE 23
#endif
#ifndef MADV_COLLAPSE  // Linux 6.1+
#define MADV_COLLAPSE 25
#endif

#ifndef PR_SET_VMA
#define PR_SET_VMA 0x53564d41
#endif
#ifndef PR_SET_VMA_ANON_NAME
#define PR_SET_VMA_ANON_NAME 0
#endif

namespace sandbox2 {
namespace {

namespace file = ::sapi::file;
namespace fileops = ::sapi::file_util::fileops;

// Validates that the path is absolute and canonical.
absl::StatusOr<std::string> ValidatePath(absl::string_view path,
                                         bool allow_relative_path = false) {
  if (path.empty()) {
    return absl::InvalidArgumentError("Path must not be empty");
  }

  if (!file::IsAbsolutePath(path) && !allow_relative_path) {
    return absl::InvalidArgumentError(
        absl::StrCat("Path must be absolute: ", path));
  }

  std::string fixed_path = file::CleanPath(path);
  if (fixed_path != path) {
    return absl::InvalidArgumentError(
        absl::StrCat("Path is not canonical: ", path));
  }

  return fixed_path;
}

constexpr uint32_t kMmapSyscalls[] = {
#ifdef __NR_mmap2
    __NR_mmap2,
#endif
#ifdef __NR_mmap
    __NR_mmap,
#endif
};

constexpr bool CheckMapExec(uint32_t num) {
  if (num == __NR_mprotect) {
    return true;
  }
#ifdef __NR_pkey_mprotect
  if (num == __NR_pkey_mprotect) {
    return true;
  }
#endif
  for (uint32_t mmap_syscall : kMmapSyscalls) {
    if (num == mmap_syscall) {
      return true;
    }
  }
  return false;
}

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

PolicyBuilder& PolicyBuilder::DisableNamespaces(NamespacesToken) {
  if (requires_namespaces_) {
    SetError(absl::FailedPreconditionError(
        "Namespaces cannot be both disabled and enabled. You're probably "
        "using features that implicitly enable namespaces (SetHostname, "
        "AddFile, AddDirectory, AddDataDependency, AddLibrariesForBinary "
        "or similar)"));
    return *this;
  }
  use_namespaces_ = false;
  return *this;
}

PolicyBuilder& PolicyBuilder::Allow(MapExec) {
  allow_map_exec_ = true;
  return *this;
}

PolicyBuilder& PolicyBuilder::Allow(SeccompSpeculation) {
  allow_speculation_ = true;
  return *this;
}

PolicyBuilder& PolicyBuilder::Allow(UnrestrictedNetworking) {
  EnableNamespaces();  // NOLINT(clang-diagnostic-deprecated-declarations)

  if (netns_mode_ != NETNS_MODE_UNSPECIFIED) {
    SetError(absl::FailedPreconditionError(absl::StrCat(
        "Incompatible with other network namespaces modes. A sandbox can have "
        "only one network namespace mode. Attempted to configure: ",
        NetNsMode_Name(netns_mode_))));
    return *this;
  }

  netns_mode_ = NETNS_MODE_NONE;
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowSyscall(uint32_t num) {
  if (handled_syscalls_.insert(num).second &&
      allowed_syscalls_.insert(num).second) {
    if (!allow_map_exec_ && CheckMapExec(num)) {
      SetError(absl::FailedPreconditionError(
          "Allowing unrestricted mmap/mprotect/pkey_mprotect requires "
          "Allow(MapExec)."));
      return *this;
    }
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
  if (handled_syscalls_.insert(num).second &&
      blocked_syscalls_.insert(num).second) {
    user_policy_.insert(user_policy_.end(), {SYSCALL(num, ERRNO(error))});
    switch (num) {
      case __NR_bpf:
        user_policy_handles_bpf_ = true;
        break;
      case __NR_ptrace:
        user_policy_handles_ptrace_ = true;
        break;
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

PolicyBuilder& PolicyBuilder::AllowEpollWait() {
  return AllowSyscalls({
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

PolicyBuilder& PolicyBuilder::AllowEpoll() {
  AllowSyscalls({
#ifdef __NR_epoll_create
      __NR_epoll_create,
#endif
#ifdef __NR_epoll_create1
      __NR_epoll_create1,
#endif
#ifdef __NR_epoll_ctl
      __NR_epoll_ctl,
#endif
  });

  return AllowEpollWait();
}

PolicyBuilder& PolicyBuilder::AllowInotifyInit() {
  return AllowSyscalls({
#ifdef __NR_inotify_init
      __NR_inotify_init,
#endif
#ifdef __NR_inotify_init1
      __NR_inotify_init1,
#endif
  });
}

PolicyBuilder& PolicyBuilder::AllowSelect() {
  return AllowSyscalls({
#ifdef __NR_select
      __NR_select,
#endif
#ifdef __NR_pselect6
      __NR_pselect6,
#endif
  });
}

PolicyBuilder& PolicyBuilder::AllowExit() {
  return AllowSyscalls({__NR_exit, __NR_exit_group});
}

PolicyBuilder& PolicyBuilder::AllowScudoMalloc() {
  if (allowed_complex_.scudo_malloc) {
    return *this;
  }
  allowed_complex_.scudo_malloc = true;
  AllowTime();
  AllowSyscalls({__NR_munmap, __NR_nanosleep});
  AllowFutexOp(FUTEX_WAKE);
  AllowLimitedMadvise();
  AllowGetRandom();
  AllowGetPIDs();
  AllowWipeOnFork();
#ifdef __NR_open
  OverridableBlockSyscallWithErrno(__NR_open, ENOENT);
#endif
#ifdef __NR_openat
  OverridableBlockSyscallWithErrno(__NR_openat, ENOENT);
#endif

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
  if (allowed_complex_.tcmalloc) {
    return *this;
  }
  allowed_complex_.tcmalloc = true;
  AllowTime();
  AllowRestartableSequences(kRequireFastFences);
  AllowSyscalls({__NR_munmap, __NR_nanosleep, __NR_brk, __NR_mincore,
                 __NR_membarrier, __NR_lseek});
  AllowLimitedMadvise();
  AllowPrctlSetVma();
  AllowPoll();
  AllowGetPIDs();

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
        JEQ32(MAP_ANONYMOUS | MAP_PRIVATE, ALLOW),
        JUMP(&labels, mmap_end),

        // PROT_NONE
        LABEL(&labels, prot_none),
        ARG_32(3),  // flags
        JEQ32(MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE, ALLOW),
        JEQ32(MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED_NOREPLACE, ALLOW),
        JEQ32(MAP_ANONYMOUS | MAP_PRIVATE, ALLOW),

        LABEL(&labels, mmap_end),
    };
  });
}

PolicyBuilder& PolicyBuilder::AllowSystemMalloc() {
  if (allowed_complex_.system_malloc) {
    return *this;
  }
  allowed_complex_.system_malloc = true;
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
  if constexpr (!sapi::sanitizers::IsAny()) {
    return *this;
  }
  if (allowed_complex_.llvm_sanitizers) {
    return *this;
  }
  allowed_complex_.llvm_sanitizers = true;
  // *san use a custom allocator that runs mmap/unmap under the hood.  For
  // example:
  // https://github.com/llvm/llvm-project/blob/596d534ac3524052df210be8d3c01a33b2260a42/compiler-rt/lib/asan/asan_allocator.cpp#L980
  // https://github.com/llvm/llvm-project/blob/62ec4ac90738a5f2d209ed28c822223e58aaaeb7/compiler-rt/lib/sanitizer_common/sanitizer_allocator_secondary.h#L98
  AllowMmapWithoutExec();
  AllowSyscall(__NR_munmap);
  AllowSyscall(__NR_sched_yield);

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
                                       JEQ32(MADV_DONTNEED, ALLOW),
                                       JEQ32(MADV_NOHUGEPAGE, ALLOW),
                                   });
  // Sanitizers read from /proc. For example:
  // https://github.com/llvm/llvm-project/blob/634da7a1c61ee8c173e90a841eb1f4ea03caa20b/compiler-rt/lib/sanitizer_common/sanitizer_linux.cpp#L1155
  AddDirectoryIfNamespaced("/proc");
  AllowOpen();
  // Sanitizers need pid for reports. For example:
  // https://github.com/llvm/llvm-project/blob/634da7a1c61ee8c173e90a841eb1f4ea03caa20b/compiler-rt/lib/sanitizer_common/sanitizer_linux.cpp#L740
  AllowGetPIDs();
  // Sanitizers may try color output. For example:
  // https://github.com/llvm/llvm-project/blob/87dd3d350c4ce0115b2cdf91d85ddd05ae2661aa/compiler-rt/lib/sanitizer_common/sanitizer_posix_libcdep.cpp#L157
  OverridableBlockSyscallWithErrno(__NR_ioctl, EPERM);
  // https://github.com/llvm/llvm-project/blob/9aa39481d9eb718e872993791547053a3c1f16d5/compiler-rt/lib/sanitizer_common/sanitizer_linux_libcdep.cpp#L150
  // https://sourceware.org/git/?p=glibc.git;a=blob;f=nptl/pthread_getattr_np.c;h=de7edfa0928224eb8375e2fe894d6677570fbb3b;hb=HEAD#l188
  AllowSyscall(__NR_sched_getaffinity);
  // https://github.com/llvm/llvm-project/blob/3cabbf60393cc8d55fe635e35e89e5973162de33/compiler-rt/lib/interception/interception.h#L352
#ifdef __ELF__
  AllowDynamicStartup(MapExec());
#endif
  // https://github.com/llvm/llvm-project/blob/02c2b472b510ff55679844c087b66e7837e13dc2/compiler-rt/lib/sanitizer_common/sanitizer_linux.cpp#L434
#ifdef __NR_readlink
  OverridableBlockSyscallWithErrno(__NR_readlink, ENOENT);
#endif
  OverridableBlockSyscallWithErrno(__NR_readlinkat, ENOENT);
  if constexpr (sapi::sanitizers::IsASan()) {
    AllowSyscall(__NR_sigaltstack);
  }
  if constexpr (sapi::sanitizers::IsTSan()) {
    AllowSyscall(__NR_set_robust_list);
  }
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowLlvmCoverage() {
  if (!sapi::IsCoverageRun()) {
    return *this;
  }
  if (allowed_complex_.llvm_coverage) {
    return *this;
  }
  allowed_complex_.llvm_coverage = true;
  AllowStat();
  AllowGetPIDs();
  AllowOpen();
  AllowRead();
  AllowWrite();
  AllowMkdir();
  AllowSafeFcntl();
  AllowSyscalls({
      __NR_munmap, __NR_close, __NR_lseek,
#ifdef __NR__llseek
      __NR__llseek,  // Newer glibc on PPC
#endif
  });
  AllowTcMalloc();
  AddPolicyOnMmap([](bpf_labels& labels) -> std::vector<sock_filter> {
    return {
        ARG_32(2),  // prot
        JNE32(PROT_READ | PROT_WRITE, JUMP(&labels, mmap_end)),
        ARG_32(3),  // flags
        JEQ32(MAP_SHARED, ALLOW),
        LABEL(&labels, mmap_end),
    };
  });
  const char* coverage_dir = std::getenv("COVERAGE_DIR");
  if (!coverage_dir || absl::string_view(coverage_dir).empty()) {
    LOG(WARNING)
        << "Environment variable COVERAGE is set but COVERAGE_DIR is not set. "
           "No directory to collect coverage data will be added to the "
           "sandbox.";
    return *this;
  }
  AddDirectoryIfNamespaced(coverage_dir, /*is_ro=*/false);
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowLimitedMadvise() {
  if (allowed_complex_.limited_madvise) {
    return *this;
  }
  allowed_complex_.limited_madvise = true;
  return AddPolicyOnSyscall(
      __NR_madvise, {
                        ARG_32(2),
                        JEQ32(MADV_SEQUENTIAL, ALLOW),
                        JEQ32(MADV_DONTNEED, ALLOW),
                        JEQ32(MADV_REMOVE, ALLOW),
                        JEQ32(MADV_HUGEPAGE, ALLOW),
                        JEQ32(MADV_NOHUGEPAGE, ALLOW),
                        JEQ32(MADV_DONTDUMP, ALLOW),
                        JEQ32(MADV_COLLAPSE, ALLOW),
                    });
}

PolicyBuilder& PolicyBuilder::AllowMadvisePopulate() {
  if (allowed_complex_.madvise_populate) {
    return *this;
  }
  allowed_complex_.madvise_populate = true;
  return AddPolicyOnSyscall(__NR_madvise, {
                                              ARG_32(2),
                                              JEQ32(MADV_POPULATE_READ, ALLOW),
                                              JEQ32(MADV_POPULATE_WRITE, ALLOW),
                                          });
}

PolicyBuilder& PolicyBuilder::AllowMmapWithoutExec() {
  if (allowed_complex_.mmap_without_exec) {
    return *this;
  }
  allowed_complex_.mmap_without_exec = true;
  return AddPolicyOnMmap({
      ARG_32(2),
      BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, PROT_EXEC, 1, 0),
      ALLOW,
  });
}

PolicyBuilder& PolicyBuilder::AllowMprotectWithoutExec() {
  if (allowed_complex_.mprotect_without_exec) {
    return *this;
  }
  allowed_complex_.mprotect_without_exec = true;
  return AddPolicyOnSyscall(
      __NR_mprotect, {
                         ARG_32(2),
                         BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, PROT_EXEC, 1, 0),
                         ALLOW,
                     });
}

PolicyBuilder& PolicyBuilder::AllowMprotect(MapExec) {
  return Allow(MapExec()).AllowSyscall(__NR_mprotect);
}

PolicyBuilder& PolicyBuilder::AllowPkeyMprotectWithoutExec() {
  if (allowed_complex_.pkey_mprotect_without_exec) {
    return *this;
  }
  allowed_complex_.pkey_mprotect_without_exec = true;
#ifdef __NR_pkey_mprotect
  AddPolicyOnSyscall(__NR_pkey_mprotect,
                     {
                         ARG_32(2),
                         BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, PROT_EXEC, 1, 0),
                         ALLOW,
                     });
  return *this;
#endif
}

PolicyBuilder& PolicyBuilder::AllowPkeyMprotect(MapExec) {
#ifdef __NR_pkey_mprotect
  Allow(MapExec()).AllowSyscall(__NR_pkey_mprotect);
#endif
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowMmap() { return AllowMmap(MapExec()); }

PolicyBuilder& PolicyBuilder::AllowMmap(MapExec) {
  return Allow(MapExec()).AllowSyscalls(kMmapSyscalls);
}

PolicyBuilder& PolicyBuilder::AllowMlock() {
#ifdef __NR_mlock
  AllowSyscall(__NR_mlock);
#endif
#ifdef __NR_munlock
  AllowSyscall(__NR_munlock);
#endif
#ifdef __NR_mlock2
  AllowSyscall(__NR_mlock2);
#endif
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowOpen() {
#ifdef __NR_creat
  AllowSyscall(__NR_creat);
#endif
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
#ifdef __NR_fstatfs
  AllowSyscall(__NR_fstatfs);
#endif
#ifdef __NR_fstatfs64
  AllowSyscall(__NR_fstatfs64);
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
#ifdef __NR_faccessat2
  AllowSyscall(__NR_faccessat2);
#endif
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowDup() {
  AllowSyscall(__NR_dup);
#ifdef __NR_dup2
  AllowSyscall(__NR_dup2);
#endif
  AllowSyscall(__NR_dup3);
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowPipe() {
#ifdef __NR_pipe
  AllowSyscall(__NR_pipe);
#endif
  AllowSyscall(__NR_pipe2);
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowChmod() {
#ifdef __NR_chmod
  AllowSyscall(__NR_chmod);
#endif
  AllowSyscall(__NR_fchmod);
  AllowSyscall(__NR_fchmodat);
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowChown() {
#ifdef __NR_chown
  AllowSyscall(__NR_chown);
#endif
#ifdef __NR_lchown
  AllowSyscall(__NR_lchown);
#endif
  AllowSyscall(__NR_fchown);
  AllowSyscall(__NR_fchownat);
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

PolicyBuilder& PolicyBuilder::AllowReadlink() {
  return AllowSyscalls({
#ifdef __NR_readlink
      __NR_readlink,
#endif
#ifdef __NR_readlinkat
      __NR_readlinkat,
#endif
  });
}

PolicyBuilder& PolicyBuilder::AllowLink() {
  return AllowSyscalls({
#ifdef __NR_link
      __NR_link,
#endif
#ifdef __NR_linkat
      __NR_linkat,
#endif
  });
}

PolicyBuilder& PolicyBuilder::AllowSymlink() {
  return AllowSyscalls({
#ifdef __NR_symlink
      __NR_symlink,
#endif
#ifdef __NR_symlinkat
      __NR_symlinkat,
#endif
  });
}

PolicyBuilder& PolicyBuilder::AllowMkdir() {
  return AllowSyscalls({
#ifdef __NR_mkdir
      __NR_mkdir,
#endif
#ifdef __NR_mkdirat
      __NR_mkdirat,
#endif
  });
}

PolicyBuilder& PolicyBuilder::AllowUtime() {
  return AllowSyscalls({
#ifdef __NR_futimens
      __NR_futimens,
#endif
#ifdef __NR_futimesat
      __NR_futimesat,
#endif
#ifdef __NR_utime
      __NR_utime,
#endif
#ifdef __NR_utimes
      __NR_utimes,
#endif
#ifdef __NR_utimensat
      __NR_utimensat,
#endif
  });
}

PolicyBuilder& PolicyBuilder::AllowSafeBpf() {
  allow_safe_bpf_ = true;
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowSafeFcntl() {
  if (allowed_complex_.safe_fcntl) {
    return *this;
  }
  allowed_complex_.safe_fcntl = true;
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

PolicyBuilder& PolicyBuilder::AllowAlarm() {
  return AllowSyscalls({
#ifdef __NR_alarm
      __NR_alarm,
#endif
      __NR_setitimer});
}

PolicyBuilder& PolicyBuilder::AllowPosixTimers() {
  return AllowSyscalls({
      __NR_timer_create,
      __NR_timer_delete,
      __NR_timer_settime,
      __NR_timer_gettime,
      __NR_timer_getoverrun,
  });
}

PolicyBuilder& PolicyBuilder::AllowHandleSignals() {
  return AllowSyscalls({
      __NR_restart_syscall,
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
  if (allowed_complex_.tcgets) {
    return *this;
  }
  allowed_complex_.tcgets = true;
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

PolicyBuilder& PolicyBuilder::AllowRestartableSequences(
    CpuFenceMode cpu_fence_mode) {
  if (!allowed_complex_.slow_fences && !allowed_complex_.fast_fences) {
#ifdef __NR_rseq
    AllowSyscall(__NR_rseq);
#endif
    AddPolicyOnMmap([](bpf_labels& labels) -> std::vector<sock_filter> {
      return {
          ARG_32(2),  // prot
          JNE32(PROT_READ | PROT_WRITE, JUMP(&labels, mmap_end)),

          ARG_32(3),  // flags
          JEQ32(MAP_PRIVATE | MAP_ANONYMOUS, ALLOW),

          LABEL(&labels, mmap_end),
      };
    });
    AllowSyscall(__NR_getcpu);
    AllowSyscall(__NR_membarrier);
    AllowFutexOp(FUTEX_WAIT);
    AllowFutexOp(FUTEX_WAKE);
    AllowRead();
    AllowOpen();
    AllowPoll();
    AllowSyscall(__NR_close);
    AddPolicyOnSyscall(__NR_rt_sigprocmask, {
                                                ARG_32(0),
                                                JEQ32(SIG_SETMASK, ALLOW),
                                            });
    AllowPrctlSetVma();

    AddFileIfNamespaced("/proc/cpuinfo");
    AddFileIfNamespaced("/proc/stat");
    AddDirectoryIfNamespaced("/sys/devices/system/cpu");
  }
  if (cpu_fence_mode == kAllowSlowFences && !allowed_complex_.slow_fences) {
    AllowSyscall(__NR_sched_getaffinity);
    AllowSyscall(__NR_sched_setaffinity);
    AddFileIfNamespaced("/proc/self/cpuset");
    allowed_complex_.slow_fences = true;
  } else if (cpu_fence_mode == kRequireFastFences) {
    allowed_complex_.fast_fences = true;
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

PolicyBuilder& PolicyBuilder::AllowGetPGIDs() {
  return AllowSyscalls({
      __NR_getpgid,
#ifdef __NR_getpgrp
      __NR_getpgrp,
#endif
  });
}

PolicyBuilder& PolicyBuilder::AllowGetRlimit() {
  if (allowed_complex_.getrlimit) {
    return *this;
  }
  allowed_complex_.getrlimit = true;
#ifdef __NR_prlimit64
  AddPolicyOnSyscall(__NR_prlimit64, {ARG(2), JEQ64(0, 0, ALLOW)});
#endif
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
#ifdef __NR_prlimit64
      __NR_prlimit64,
#endif
#ifdef __NR_setrlimit
      __NR_setrlimit,
#endif
#ifdef __NR_usetrlimit
      __NR_usetrlimit,
#endif
  });
}

PolicyBuilder& PolicyBuilder::AllowGetRandom() {
  if (allowed_complex_.getrandom) {
    return *this;
  }
  allowed_complex_.getrandom = true;
  return AddPolicyOnSyscall(__NR_getrandom, {
                                                ARG_32(2),
                                                JEQ32(0, ALLOW),
                                                JEQ32(GRND_NONBLOCK, ALLOW),
                                            });
}

PolicyBuilder& PolicyBuilder::AllowWipeOnFork() {
  if (allowed_complex_.wipe_on_fork) {
    return *this;
  }
  allowed_complex_.wipe_on_fork = true;
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
  if (allowed_complex_.log_forwarding) {
    return *this;
  }
  allowed_complex_.log_forwarding = true;
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
  AllowGetRlimit();

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
#ifdef __NR_rmdir
      __NR_rmdir,
#endif
#ifdef __NR_unlink
      __NR_unlink,
#endif
      __NR_unlinkat,
  });
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowPoll() {
  AllowSyscalls({
#ifdef __NR_poll
      __NR_poll,
#endif
      __NR_ppoll,
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

PolicyBuilder& PolicyBuilder::AllowEventFd() {
  AllowSyscalls({
#ifdef __NR_eventfd
      __NR_eventfd,
#endif
      __NR_eventfd2,
  });
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowPrctlSetName() {
  if (allowed_complex_.prctl_set_name) {
    return *this;
  }
  allowed_complex_.prctl_set_name = true;
  AddPolicyOnSyscall(__NR_prctl, {ARG_32(0), JEQ32(PR_SET_NAME, ALLOW)});
  return *this;
}

PolicyBuilder& PolicyBuilder::AllowPrctlSetVma() {
  if (allowed_complex_.prctl_set_vma) {
    return *this;
  }
  allowed_complex_.prctl_set_vma = true;
  AddPolicyOnSyscall(__NR_prctl,
                     [](bpf_labels& labels) -> std::vector<sock_filter> {
                       return {
                           ARG_32(0),
                           JNE32(PR_SET_VMA, JUMP(&labels, prctlsetvma_end)),
                           ARG_32(1),
                           JEQ32(PR_SET_VMA_ANON_NAME, ALLOW),
                           LABEL(&labels, prctlsetvma_end),
                       };
                     });
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
  if (allowed_complex_.static_startup) {
    return *this;
  }
  allowed_complex_.static_startup = true;
  AllowGetRlimit();
  AllowSyscalls({
      // These syscalls take a pointer, so no restriction.
      __NR_uname,
      __NR_brk,
      __NR_set_tid_address,

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

  AllowGetRlimit();
  AddPolicyOnSyscall(__NR_mprotect, {
                                        ARG_32(2),
                                        JEQ32(PROT_READ, ALLOW),
                                    });

  OverridableBlockSyscallWithErrno(__NR_sigaltstack, ENOSYS);

  return *this;
}

PolicyBuilder& PolicyBuilder::AllowDynamicStartup() {
  return AllowDynamicStartup(MapExec());
}

PolicyBuilder& PolicyBuilder::AllowDynamicStartup(MapExec) {
  Allow(MapExec());
  if (allowed_complex_.dynamic_startup) {
    return *this;
  }
  allowed_complex_.dynamic_startup = true;

  AllowAccess();
  AllowOpen();
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
        JEQ32(MAP_FILE | MAP_PRIVATE | MAP_DENYWRITE | MAP_FIXED, ALLOW),

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
  if (nums.empty()) {
    SetError(absl::InvalidArgumentError(
        "Cannot add a policy for empty list of syscalls"));
    return *this;
  }
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
    if (*it == __NR_bpf || *it == __NR_ptrace) {
      SetError(absl::InvalidArgumentError(
          "cannot add policy for bpf/ptrace syscall"));
      return *this;
    }
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
  custom_policy_syscalls_.insert(nums.begin(), nums.end());
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
  return DefaultAction(AllowAllSyscalls());
}

PolicyBuilder& PolicyBuilder::DefaultAction(AllowAllSyscalls) {
  default_action_ = ALLOW;
  return *this;
}

PolicyBuilder& PolicyBuilder::DefaultAction(TraceAllSyscalls) {
  default_action_ = SANDBOX2_TRACE;
  return *this;
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
  if (!last_status_.ok()) {
    return last_status_;
  }

  if (user_policy_.size() > kMaxUserPolicyLength) {
    return absl::FailedPreconditionError(
        absl::StrCat("User syscall policy is to long (", user_policy_.size(),
                     " > ", kMaxUserPolicyLength, ")."));
  }

  if (already_built_) {
    return absl::FailedPreconditionError("Can only build policy once.");
  }

  // Using `new` to access a non-public constructor.
  auto policy = absl::WrapUnique(new Policy());

  if (use_namespaces_) {
    // If no specific netns mode is set, default to per-sandboxee.
    if (netns_mode_ == NETNS_MODE_UNSPECIFIED) {
      netns_mode_ = NETNS_MODE_PER_SANDBOXEE;
    }
    if (netns_mode_ == NETNS_MODE_NONE && hostname_ != kDefaultHostname) {
      return absl::FailedPreconditionError(
          "Cannot set hostname without network namespaces.");
    }
    policy->namespace_ = Namespace(std::move(mounts_), hostname_, netns_mode_,
                                   allow_mount_propagation_);
  }

  policy->allow_map_exec_ = allow_map_exec_;
  policy->allow_safe_bpf_ = allow_safe_bpf_;
  policy->allow_speculation_ = allow_speculation_;
  policy->collect_stacktrace_on_signal_ = collect_stacktrace_on_signal_;
  policy->collect_stacktrace_on_violation_ = collect_stacktrace_on_violation_;
  policy->collect_stacktrace_on_timeout_ = collect_stacktrace_on_timeout_;
  policy->collect_stacktrace_on_kill_ = collect_stacktrace_on_kill_;
  policy->collect_stacktrace_on_exit_ = collect_stacktrace_on_exit_;
  policy->user_policy_ = std::move(user_policy_);
  if (default_action_) {
    policy->user_policy_.push_back(*default_action_);
  }
  policy->user_policy_.insert(policy->user_policy_.end(),
                              overridable_policy_.begin(),
                              overridable_policy_.end());
  policy->user_policy_handles_bpf_ = user_policy_handles_bpf_;
  policy->user_policy_handles_ptrace_ = user_policy_handles_ptrace_;

  policy->allowed_hosts_ = std::move(allowed_hosts_);
  already_built_ = true;
  return std::move(policy);
}

PolicyBuilder& PolicyBuilder::AddFile(absl::string_view path, bool is_ro) {
  return AddFileAt(path, path, is_ro);
}

PolicyBuilder& PolicyBuilder::AddFileAt(absl::string_view outside,
                                        absl::string_view inside, bool is_ro) {
  EnableNamespaces();  // NOLINT(clang-diagnostic-deprecated-declarations)
  return AddFileAtIfNamespaced(outside, inside, is_ro);
}

PolicyBuilder& PolicyBuilder::AddFileIfNamespaced(absl::string_view path,
                                                  bool is_ro) {
  return AddFileAtIfNamespaced(path, path, is_ro);
}

PolicyBuilder& PolicyBuilder::AddFileAtIfNamespaced(absl::string_view outside,
                                                    absl::string_view inside,
                                                    bool is_ro) {
  auto valid_outside = ValidatePath(outside);
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

  auto valid_path = ValidatePath(path, /*allow_relative_path=*/true);
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
  return AddDirectoryAtIfNamespaced(outside, inside, is_ro);
}

PolicyBuilder& PolicyBuilder::AddDirectoryIfNamespaced(absl::string_view path,
                                                       bool is_ro) {
  return AddDirectoryAtIfNamespaced(path, path, is_ro);
}

PolicyBuilder& PolicyBuilder::AddDirectoryAtIfNamespaced(
    absl::string_view outside, absl::string_view inside, bool is_ro) {
  auto valid_outside = ValidatePath(outside);
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

// Use Allow(sandbox2::UnrestrictedNetworking()) instead.
PolicyBuilder& PolicyBuilder::AllowUnrestrictedNetworking() {
  return Allow(UnrestrictedNetworking());
}

PolicyBuilder& PolicyBuilder::UseForkServerSharedNetNs() {
  EnableNamespaces();  // NOLINT(clang-diagnostic-deprecated-declarations)

  if (netns_mode_ != NETNS_MODE_UNSPECIFIED) {
    SetError(absl::FailedPreconditionError(absl::StrCat(
        "Incompatible with other network namespaces modes. A sandbox can have "
        "only one network namespace mode. Attempted to configure: ",
        NetNsMode_Name(netns_mode_))));
    return *this;
  }

  netns_mode_ = NETNS_MODE_SHARED_PER_FORKSERVER;
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

  AllowSafeFcntl();
  AllowFutexOp(FUTEX_WAKE);
  AllowFutexOp(FUTEX_WAIT);
  AllowFutexOp(FUTEX_WAIT_BITSET);
  AllowDup();
  AllowSyscalls({
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
  if (handled_syscalls_.insert(__NR_ptrace).second) {
    user_policy_.insert(user_policy_.end(), {SYSCALL(__NR_ptrace, TRAP(0))});
    user_policy_handles_ptrace_ = true;
  }
  return *this;
}

PolicyBuilder& PolicyBuilder::SetRootWritable() {
  EnableNamespaces();  // NOLINT(clang-diagnostic-deprecated-declarations)
  mounts_.SetRootWritable();

  return *this;
}

PolicyBuilder& PolicyBuilder::Allow(MountPropagation) {
  allow_mount_propagation_ = true;
  return *this;
}

PolicyBuilder& PolicyBuilder::Allow(MountPropagation,
                                    absl::string_view inside) {
  if (absl::Status status = mounts_.AllowMountPropagation(inside);
      !status.ok()) {
    SetError(status);
  }
  return *this;
}

PolicyBuilder& PolicyBuilder::DangerAllowMountPropagation() {
  return Allow(MountPropagation());
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

std::string PolicyBuilder::AnchorPathAbsolute(absl::string_view relative_path,
                                              absl::string_view base) {
  if (relative_path.empty()) {
    LOG(ERROR) << "Passed relative_path is empty";
    return "";
  }

  if (file::IsAbsolutePath(relative_path)) {
    VLOG(3) << "Nothing to do, relative_path is absolute";
    return std::string(relative_path);
  }

  std::string clean_path = file::CleanPath(relative_path);
  if (absl::StartsWith(clean_path, "../") || clean_path == "..") {
    LOG(ERROR)
        << "Anchored path would be outside of base because relative_path: '"
        << relative_path << "' starts with '..'";
    return "";
  }

  if (file::IsAbsolutePath(base)) {
    return file::CleanPath(file::JoinPath(base, clean_path));
  }

  std::string cwd = fileops::GetCWD();
  if (cwd.empty()) {
    LOG(ERROR) << "Failed to get current working directory";
    return "";
  }

  if (base.empty()) {
    VLOG(1) << "Using current working directory as base is empty";
    // CWD is guaranteed to exist and clean_path is guaranteed to not start with
    // '..'.
    return file::CleanPath(file::JoinPath(cwd, clean_path));
  }

  return file::CleanPath(file::JoinPath(cwd, base, clean_path));
}

}  // namespace sandbox2
