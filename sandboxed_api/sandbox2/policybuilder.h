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

#ifndef SANDBOXED_API_SANDBOX2_POLICYBUILDER_H_
#define SANDBOXED_API_SANDBOX2_POLICYBUILDER_H_

#include <linux/filter.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/sandbox2/mounts.h"
#include "sandboxed_api/sandbox2/network_proxy/filtering.h"
#include "sandboxed_api/sandbox2/policy.h"

struct bpf_labels;

namespace sandbox2 {

class AllowAllSyscalls;
class LoadUserBpfCodeFromFile;
class MapExec;
class MountPropagation;
class NamespacesToken;
class UnrestrictedNetworking;
class UnsafeCoreDumpPtrace;
class SeccompSpeculation;
class TraceAllSyscalls;
class WriteExecutable;

// PolicyBuilder is a helper class to simplify creation of policies. The builder
// uses fluent interface for convenience and increased readability of policies.
//
// To build a policy you simply create a new builder object, call methods on it
// specifying what you want and finally call `BuildOrDie()` to generate you
// policy.
//
// For instance this would generate a simple policy suitable for binaries doing
// only computations:
//
// ```c++
// std::unique_ptr<Policy> policy =
//     PolicyBuilder()
//       .AllowRead()
//       .AllowWrite()
//       .AllowExit()
//       .AllowSystemMalloc()
//       .BuildOrDie();
// ```
//
// Operations are executed in the order they are dictated, though in most cases
// this has no influence since the operations themselves commute.
//
// For instance these two policies are equivalent:
//
// ```c++
// auto policy = PolicyBuilder.AllowRead().AllowWrite().BuildOrDie();
// auto policy = PolicyBuilder.AllowWrite().AllowRead().BuildOrDie();
// ```
//
// While these two are not:
//
//
// ```c++
// auto policy = PolicyBuilder.AllowRead().BlockSyscallWithErrno(__NR_read, EIO)
//                            .BuildOrDie();
// auto policy = PolicyBuilder.BlockSyscallWithErrno(__NR_read, EIO).AllowRead()
//                            .BuildOrDie();
// ```
//
// In fact the first one is equivalent to:
//
// ```c++
// auto policy = PolicyBuilder.AllowRead().BuildOrDie();
// ```
//
// If you dislike the chained style, it is also possible to write the first
// example as this:
//
// ```c++
// PolicyBuilder builder;
// builder.AllowRead();
// builder.AllowWrite();
// builder.AllowExit();
// builder.AllowSystemMalloc();
// auto policy = builder.BuildOrDie();
// ```
//
// For a more complicated example, see examples/static/static_sandbox.cc
class PolicyBuilder final {
 public:
  // Possible CPU fence modes for `AllowRestartableSequences()`
  enum CpuFenceMode {
    // Allow only fast fences for restartable sequences.
    kRequireFastFences,

    // Allow fast fences as well as slow fences if fast fences are unavailable.
    kAllowSlowFences,
  };

  static constexpr absl::string_view kDefaultHostname = "sandbox2";

  // Seccomp takes a 16-bit filter length, so the limit would be 64k.
  //
  // We set it lower so that there is for sure some room for the default policy.
  static constexpr size_t kMaxUserPolicyLength = 30000;

  using BpfFunc = const std::function<std::vector<sock_filter>(bpf_labels&)>&;

  // Appends code to allow visibility restricted policy functionality.
  //
  // For example:
  // `Allow(sandbox2::UnrestrictedNetworking);`
  // This allows unrestricted network access by not creating a network
  // namespace.
  //
  // Each `type T` is defined in an individual library and individually
  // visibility restricted.
  template <typename... T>
  PolicyBuilder& Allow(T... tags) {
    return (Allow(tags), ...);
  }

  // Disables the use of namespaces.
  //
  // The default security posture of Sandbox2 depends on the use of namespaces
  // and syscall filters. By disabling namespaces, the default security posture
  // is weakened.
  //
  // The consequence of disabling namespaces is that the sandboxee will be able
  // to access the host's file system, network, and other resources if the
  // appropriate syscalls are also allowed.
  //
  // Disabling namespaces is not recommended and should only be done if
  // absolutely necessary.
  PolicyBuilder& DisableNamespaces(NamespacesToken);

  // Allows the use of memory mappings that are marked as executable.
  //
  // This applies to the mmap and mprotect syscalls and by default, mapped
  // memory pages are not allowed to be marked as both writable and executable.
  //
  // The use of this API is usually only necessary for JIT engines. To
  // actually allow executable mappings, the respective mmap()/mprotect()
  // syscalls need to be added to the policy as well.
  PolicyBuilder& Allow(MapExec);

  // Allows the sandboxee to benefit from speculative execution.
  //
  // By default and on recent (6.x) kernels, additional mitigations are enabled
  // to prevent speculative execution attacks. This call disables those
  // mitigations to reclaim some of the performance overhead.
  //
  // NOTE: The performance benefits of using this API are highly dependent on
  // the host CPU architecture and the workload running inside the sandbox.
  // The Linux kernel will disable both the IBPB and STIBP mitigations for the
  // the sandboxee on CPUs that support this.
  //
  // On newer AMD processors, such as Milan or Genoa, this leads to having fewer
  // branch mispredictions and thus improved performance. However, forcing STIBP
  // to be enabled on the machine level is even better, as those CPUs optimize
  // for this.
  //
  // This is an advanced API, so users should make sure they understand the
  // risks. Do not use in environments with untrusted code and/or data.
  PolicyBuilder& Allow(SeccompSpeculation);

  // Allows unrestricted access to the network by *not* creating a network
  // namespace.
  //
  // This only disables the network namespace. To actually allow networking,
  // you would also need to allow networking syscalls.
  //
  // NOTE: Requires namespace support.
  PolicyBuilder& Allow(UnrestrictedNetworking);

  // Appends code to allow a specific syscall.
  PolicyBuilder& AllowSyscall(uint32_t num);

  // Appends code to allow a number of syscalls.
  PolicyBuilder& AllowSyscalls(absl::Span<const uint32_t> nums);

  // Appends code to block a syscalls while setting errno to the error given.
  PolicyBuilder& BlockSyscallsWithErrno(absl::Span<const uint32_t> nums,
                                        int error);

  // Appends code to block a specific syscall and setting errno.
  PolicyBuilder& BlockSyscallWithErrno(uint32_t num, int error);

  // Appends code to allow waiting for events on epoll file descriptors.
  //
  // Allows these syscalls:
  // - epoll_wait
  // - epoll_pwait
  // - epoll_pwait2
  PolicyBuilder& AllowEpollWait();

  // Appends code to allow using epoll.
  //
  // Allows these syscalls:
  // - epoll_create
  // - epoll_create1
  // - epoll_ctl
  // - epoll_wait
  // - epoll_pwait
  // - epoll_pwait2
  PolicyBuilder& AllowEpoll();

  // Appends code to allow the inotify API.
  //
  // Allows these syscalls:
  // - inotify_init
  // - inotify_init1
  // - inotify_add_watch
  // - inotify_rm_watch
  // - close
  PolicyBuilder& AllowInotify();

  // Appends code to allow synchronous I/O multiplexing.
  //
  // Allows these syscalls:
  // - pselect6
  // - select
  PolicyBuilder& AllowSelect();

  // Appends code to allow exiting.
  //
  // Allows these syscalls:
  // - exit
  // - exit_group
  PolicyBuilder& AllowExit();

  // Appends code to allow restartable sequences and necessary /proc files.
  //
  // Allows these syscalls:
  // - rseq
  // - mmap(..., PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, ...)
  // - getcpu
  // - membarrier
  // - futex(WAIT)
  // - futex(WAKE)
  // - rt_sigprocmask(SIG_SETMASK)
  // Allows these files:
  // - "/proc/cpuinfo"
  // - "/proc/stat"
  // And this directory (including subdirs/files):
  // - "/sys/devices/system/cpu/"
  //
  // If `cpu_fence_mode` is `kAllowSlowFences`, also permits slow CPU fences.
  // Allows these syscalls:
  // - sched_getaffinity
  // - sched_setaffinity
  // Allows these files:
  // - "/proc/self/cpuset"
  //
  // If `cpu_fence_mode` is `kRequireFastFences`, RSEQ functionality may not
  // be enabled if fast CPU fences are not available.
  PolicyBuilder& AllowRestartableSequences(CpuFenceMode cpu_fence_mode);
  ABSL_DEPRECATE_AND_INLINE()
  PolicyBuilder& AllowRestartableSequencesWithProcFiles(
      CpuFenceMode cpu_fence_mode) {
    return this->AllowRestartableSequences(cpu_fence_mode);
  }

  // Appends code to allow the scudo version of malloc, free and
  // friends.
  //
  // This should be used in conjunction with namespaces. If scudo
  // options are passed to the sandboxee through an environment variable, access
  // to "/proc/self/environ" will have to be allowed by the policy.
  //
  // NOTE: This function is tuned towards the secure scudo allocator. If you are
  //       using another implementation, this function might not be the most
  //       suitable.
  PolicyBuilder& AllowScudoMalloc();

  // Appends code to allow the system-allocator version of malloc, free and
  // friends.
  //
  // NOTE: This function is tuned towards the malloc implementation in glibc. If
  //       you are using another implementation, this function might not be the
  //       most suitable.
  PolicyBuilder& AllowSystemMalloc();

  // Appends code to allow the tcmalloc version of malloc, free and
  // friends.
  PolicyBuilder& AllowTcMalloc();

  // Appends code to allow syscalls typically used by the LLVM sanitizers: ASAN,
  // MSAN, TSAN.
  //
  // NOTE: This method is intended as a best effort for adding syscalls that
  // are common to many binaries. It may not be fully inclusive of all potential
  // syscalls for all binaries.
  PolicyBuilder& AllowLlvmSanitizers();

  // Appends code to allow syscalls typically used by the LLVM coverage.
  //
  // NOTE: This method is intended as a best effort.
  PolicyBuilder& AllowLlvmCoverage();

  // Appends code to unconditionally allow mmap. Specifically this allows mmap
  // and mmap2 syscall on architectures where these syscalls exist.
  //
  // Prefer using `AllowMmapWithoutExec()` as allowing mapping executable pages
  // makes exploitation easier.
  PolicyBuilder& AllowMmap(MapExec);

  ABSL_DEPRECATED("Use AllowMmap(MapExec) or AllowMmapWithoutExec() instead.")
  PolicyBuilder& AllowMmap();

  // Appends code to allow mmap calls that don't specify PROT_EXEC.
  PolicyBuilder& AllowMmapWithoutExec();

  // Appends code to allow mprotect (also with PROT_EXEC).
  PolicyBuilder& AllowMprotect(MapExec);

  // Appends code to allow mprotect calls that don't specify PROT_EXEC.
  PolicyBuilder& AllowMprotectWithoutExec();

  // Appends code to allow pkey_mprotect (also with PROT_EXEC).
  PolicyBuilder& AllowPkeyMprotect(MapExec);

  // Appends code to allow pkey_mprotect calls that don't specify PROT_EXEC.
  PolicyBuilder& AllowPkeyMprotectWithoutExec();

  // Appends code to allow mlock and munlock calls.
  PolicyBuilder& AllowMlock();

  // Appends code to allow calling futex with the given operation.
  PolicyBuilder& AllowFutexOp(int op);

  // Appends code to allow opening and possibly creating files or directories.
  //
  // Allows these syscalls:
  // - creat
  // - open
  // - openat
  PolicyBuilder& AllowOpen();

  // Appends code to allow calling stat, fstat and lstat.
  //
  // Allows these syscalls:
  // - fstat
  // - fstat64
  // - fstatat
  // - fstatat64
  // - fstatfs
  // - fstatfs64
  // - lstat
  // - lstat64
  // - newfstatat
  // - oldfstat
  // - oldlstat
  // - oldstat
  // - stat
  // - stat64
  // - statfs
  // - statfs64
  // - ustat
  PolicyBuilder& AllowStat();

  // Appends code to allow checking file permissions.
  //
  // Allows these syscalls:
  // - access
  // - faccessat
  PolicyBuilder& AllowAccess();

  // Appends code to allow duplicating file descriptors.
  //
  // Allows these syscalls:
  // - dup
  // - dup2
  // - dup3
  PolicyBuilder& AllowDup();

  // Appends code to allow creating pipes.
  //
  // Allows these syscalls:
  // - pipe
  // - pipe2
  PolicyBuilder& AllowPipe();

  // Appends code to allow changing file permissions.
  //
  // Allows these syscalls:
  // - chmod
  // - fchmod
  // - fchmodat
  PolicyBuilder& AllowChmod();

  // Appends code to allow changing file ownership.
  //
  // Allows these syscalls:
  // - chown
  // - lchown
  // - fchown
  // - fchownat
  PolicyBuilder& AllowChown();

  // Appends code to the policy to allow reading from file descriptors.
  //
  // Allows these syscalls:
  // - read
  // - readv
  // - preadv
  // - pread64
  PolicyBuilder& AllowRead();

  // Appends code to the policy to allow writing to file descriptors.
  //
  // Allows these syscalls:
  // - write
  // - writev
  // - pwritev
  // - pwrite64
  PolicyBuilder& AllowWrite();

  // Appends code to allow reading directories.
  //
  // Allows these syscalls:
  // - getdents
  // - getdents64
  PolicyBuilder& AllowReaddir();

  // Appends code to allow reading symbolic links.
  //
  // Allows these syscalls:
  // - readlink
  // - readlinkat
  PolicyBuilder& AllowReadlink();

  // Appends code to allow creating links.
  //
  // Allows these syscalls:
  // - link
  // - linkat
  PolicyBuilder& AllowLink();

  // Appends code to allow creating symbolic links.
  //
  // Allows these syscalls:
  // - symlink
  // - symlinkat
  PolicyBuilder& AllowSymlink();

  // Appends code to allow creating directories.
  //
  // Allows these syscalls:
  // - mkdir
  // - mkdirat
  PolicyBuilder& AllowMkdir();

  // Appends code to allow changing file timestamps.
  //
  // Allows these syscalls:
  // - futimens
  // - futimesat
  // - utime
  // - utimensat
  // - utimes
  PolicyBuilder& AllowUtime();

  // Appends code to allow safe calls to bpf.
  //
  // Allows this syscall:
  // - bpf
  //
  // The above is only allowed when the cmd is one of:
  // BPF_MAP_LOOKUP_ELEM, BPF_OBJ_GET, BPF_MAP_GET_NEXT_KEY,
  // BPF_MAP_GET_FD_BY_ID, BPF_OBJ_GET_INFO_BY_FD
  PolicyBuilder& AllowSafeBpf();

  // Appends code to allow safe calls to fcntl.
  //
  // Allows these syscalls:
  // - fcntl
  // - fcntl64 (on architectures where it exists)
  //
  // The above are only allowed when the cmd is one of:
  // F_GETFD, F_SETFD, F_GETFL, F_SETFL, F_GETLK, F_SETLKW, F_SETLK,
  // F_DUPFD, F_DUPFD_CLOEXEC
  PolicyBuilder& AllowSafeFcntl();

  // Appends code to allow creating new processes.
  //
  // Allows these syscalls:
  // - fork
  // - vfork
  // - clone
  //
  // NOTE: While this function allows the calls, the default policy is run first
  // and it has checks for dangerous flags which can create a violation. See
  // sandbox2/policy.cc for more details.
  PolicyBuilder& AllowFork();

  // Appends code to allow waiting for processes.
  //
  // Allows these syscalls:
  // - waitpid (on architectures where it exists)
  // - wait4
  PolicyBuilder& AllowWait();

  // Appends code to allow setting alarms / interval timers.
  //
  // Allows these syscalls:
  // - alarm (on architectures where it exists)
  // - setitimer
  PolicyBuilder& AllowAlarm();

  // Appends code to allow setting posix timers.
  //
  // Allows these syscalls:
  // - timer_create
  // - timer_delete
  // - timer_settime
  // - timer_gettime
  // - timer_getoverrun
  PolicyBuilder& AllowPosixTimers();

  // Appends code to allow setting up signal handlers, returning from them, etc.
  //
  // Allows these syscalls:
  // - rt_sigaction
  // - rt_sigreturn
  // - rt_procmask
  // - signal (on architectures where it exists)
  // - sigaction (on architectures where it exists)
  // - sigreturn (on architectures where it exists)
  // - sigprocmask (on architectures where it exists)
  PolicyBuilder& AllowHandleSignals();

  // Appends code to allow doing the TCGETS ioctl.
  //
  // Allows these syscalls:
  // - ioctl (when the first argument is TCGETS)
  PolicyBuilder& AllowTCGETS();

  // Appends code to allow to getting the current time.
  //
  // Allows these syscalls:
  // - time
  // - gettimeofday
  // - clock_gettime
  PolicyBuilder& AllowTime();

  // Appends code to allow sleeping in the current thread.
  //
  // Allow these syscalls:
  // - clock_nanosleep
  // - nanosleep
  PolicyBuilder& AllowSleep();

  // Appends code to allow getting the uid, euid, gid, etc.
  //
  // Allows these syscalls:
  // - getuid + geteuid + getresuid
  // - getgid + getegid + getresgid
  // - getuid32 + geteuid32 + getresuid32 (on architectures where they exist)
  // - getgid32 + getegid32 + getresgid32 (on architectures where they exist)
  // - getgroups
  PolicyBuilder& AllowGetIDs();

  // Appends code to allow getting the pid, ppid and tid.
  //
  // Allows these syscalls:
  // - getpid
  // - getppid
  // - gettid
  PolicyBuilder& AllowGetPIDs();

  // Appends code to allow getting process groups.
  //
  // Allows these syscalls:
  // - getpgid
  // - getpgrp
  PolicyBuilder& AllowGetPGIDs();

  // Appends code to allow getting the rlimits.
  //
  // Allows these syscalls:
  // - getrlimit
  // - ugetrlimit (on architectures where it exist)
  PolicyBuilder& AllowGetRlimit();

  // Appends code to allow setting the rlimits.
  //
  // Allows these syscalls:
  // - setrlimit
  // - usetrlimit (on architectures where it exist)
  PolicyBuilder& AllowSetRlimit();

  // Appends code to allow reading random bytes.
  //
  // Allows these syscalls:
  // - getrandom (with no flags or GRND_NONBLOCK)
  //
  PolicyBuilder& AllowGetRandom();

  // Appends code to allow configuring wipe-on-fork memory.
  //
  // Allows these syscalls:
  // - madvise (with advice equal to -1 or MADV_WIPEONFORK).
  PolicyBuilder& AllowWipeOnFork();

  // Enables syscalls required to use the logging support enabled via
  // `Client::SendLogsToSupervisor()`
  //
  // Allows the following:
  // - Writes
  // - kill(0, SIGABRT) (for LOG(FATAL))
  // - clock_gettime
  // - gettid
  // - close
  PolicyBuilder& AllowLogForwarding();

  // Appends code to allow deleting files and directories.
  //
  // Allows these syscalls:
  // - rmdir (if available)
  // - unlink (if available)
  // - unlinkat
  PolicyBuilder& AllowUnlink();

  // Appends code to allow renaming files.
  //
  // Allows these syscalls:
  // - rename (if available)
  // - renameat
  // - renameat2
  PolicyBuilder& AllowRename();

  // Appends code to allow creating event notification file descriptors.
  //
  // Allows these syscalls:
  // - eventfd (if available)
  // - eventfd2
  PolicyBuilder& AllowEventFd();

  // Appends code to allow polling files.
  //
  // Allows these syscalls:
  // - poll (if available)
  // - ppoll
  PolicyBuilder& AllowPoll();

  // Appends code to allow setting the name of a thread.
  //
  // Allows the following
  // - prctl(PR_SET_NAME, ...)
  PolicyBuilder& AllowPrctlSetName();

  // Appends code to allow setting a name for an anonymous memory region.
  //
  // Allows the following
  // - prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, ...)
  PolicyBuilder& AllowPrctlSetVma();

  // Enables the syscalls necessary to start a statically linked binary.
  //
  // The current list of allowed syscalls are below. However you should *not*
  // depend on the specifics, as these will change whenever the startup code
  // changes.
  //
  // - uname,
  // - brk,
  // - set_tid_address,
  // - set_robust_list,
  // - futex(FUTEX_WAIT_BITSET, ...)
  // - rt_sigaction(0x20, ...)
  // - rt_sigaction(0x21, ...)
  // - rt_sigprocmask(SIG_UNBLOCK, ...)
  // - arch_prctl(ARCH_SET_FS)
  //
  // NOTE: This will call `BlockSyscallWithErrno(__NR_readlink, ENOENT)`. If you
  // do not want readlink blocked, put a different call before this call.
  PolicyBuilder& AllowStaticStartup();

  // Enables the syscalls necessary to start a dynamically linked binary.
  //
  // In addition to syscalls allowed by `AllowStaticStartup`, also allow
  // reading, seeking, mmap()-ing and closing files.
  PolicyBuilder& AllowDynamicStartup(MapExec);

  // Enables the syscalls necessary to use sandbox2's shared memory feature.
  //
  // This allows stat(2), munmap(2), mmap(2) with MAP_SHARED.
  PolicyBuilder& AllowSharedMemory();

  ABSL_DEPRECATED("Use AllowDynamicStartup(MapExec) instead.")
  PolicyBuilder& AllowDynamicStartup();

  // Appends a policy, which will be run on the specified syscall.
  //
  // NOTE: This policy must be written without labels. If you need labels, use
  // the overloaded function passing a BpfFunc object instead of the
  // sock_filter.
  PolicyBuilder& AddPolicyOnSyscall(uint32_t num,
                                    absl::Span<const sock_filter> policy);

  // Appends a policy, which will be run on the specified syscall.
  //
  // Example of how to use it:
  //  builder.AddPolicyOnSyscall(
  //      __NR_socket, [](bpf_labels& labels) -> std::vector<sock_filter> {
  //        return {
  //            ARG(0),  // domain is first argument of socket
  //            JEQ(AF_UNIX, JUMP(&labels, af_unix)),
  //            JEQ(AF_NETLINK, JUMP(&labels, af_netlink)),
  //            KILL,
  //
  //            LABEL(&labels, af_unix),
  //            ARG(1),
  //            JEQ(SOCK_STREAM | SOCK_NONBLOCK, ALLOW),
  //            KILL,
  //
  //            LABEL(&labels, af_netlink),
  //            ARG(2),
  //            JEQ(NETLINK_ROUTE, ALLOW),
  //        };
  //      });
  //
  // NOTE: This policy may use labels.
  PolicyBuilder& AddPolicyOnSyscall(uint32_t num, BpfFunc f);

  // Appends a policy, which will be run on the specified syscalls.
  //
  // NOTE: This policy must be written without labels.
  PolicyBuilder& AddPolicyOnSyscalls(absl::Span<const uint32_t> nums,
                                     absl::Span<const sock_filter> policy);

  // Appends a policy, which will be run on the specified syscalls.
  //
  // NOTE: This policy may use labels.
  PolicyBuilder& AddPolicyOnSyscalls(absl::Span<const uint32_t> nums,
                                     BpfFunc f);

  // Equivalent to `AddPolicyOnSyscalls(mmap_syscalls, policy)`, where
  // mmap_syscalls is a subset of {__NR_mmap, __NR_mmap2}, which exists on the
  // target architecture.
  //
  // NOTE: This policy must be written without labels.
  PolicyBuilder& AddPolicyOnMmap(absl::Span<const sock_filter> policy);

  // Equivalent to `AddPolicyOnSyscalls(mmap_syscalls, f)`, where mmap_syscalls
  // is a subset of {__NR_mmap, __NR_mmap2}, which exists on the target
  // architecture.
  //
  // NOTE: This policy may use labels.
  PolicyBuilder& AddPolicyOnMmap(BpfFunc f);

  // Builds the policy returning a unique_ptr to it or status if an error
  // happened.
  //
  // NOTE: This should only be called once.
  absl::StatusOr<std::unique_ptr<Policy>> TryBuild();

  // Builds the policy returning a unique_ptr to it.
  //
  // NOTE: This function will abort if an error happened in any of the
  // PolicyBuilder methods. This should only be called once.
  std::unique_ptr<Policy> BuildOrDie() {
    absl::StatusOr<std::unique_ptr<Policy>> policy = TryBuild();
    CHECK_OK(policy);
    return *std::move(policy);
  }

  // Adds a bind-mount for a file from outside the namespace to inside.
  //
  // This will also create parent directories inside the namespace if needed.
  //
  // NOTE: Requires namespace support.
  PolicyBuilder& AddFile(absl::string_view path, bool is_ro = true);
  PolicyBuilder& AddFileAt(absl::string_view outside, absl::string_view inside,
                           bool is_ro = true);
  // Same as `AddFile`, but no error is raised if the file does not exist.
  PolicyBuilder& AddFileIfExists(absl::string_view path, bool is_ro = true);

  // Adds the libraries and linker required by a binary.
  //
  // This does not add the binary itself, only the libraries it depends on. It
  // should work correctly for most binaries, but you might need to tweak it in
  // some cases. Run `ldd` yourself and use `AddFile` or `AddDirectory`.
  //
  // This function is safe even for untrusted/potentially malicious binaries. It
  // adds libraries only from standard library dirs and ld_library_path.
  //
  // NOTE: Requires namespace support. This method is intended as a best effort
  PolicyBuilder& AddLibrariesForBinary(absl::string_view path,
                                       absl::string_view ld_library_path = {});

  // Similar to `AddLibrariesForBinary`, but the binary is specified with an
  // open fd.
  //
  // NOTE: Requires namespace support.
  PolicyBuilder& AddLibrariesForBinary(int fd,
                                       absl::string_view ld_library_path = {});

  // Adds a bind-mount for a directory from outside the namespace to inside.
  //
  // This will also create parent directories inside the namespace if needed.
  //
  // If the directory contains symlinks, they might still be inaccessible
  // inside the sandbox (resulting in ENOENT). For example, the symlinks might
  // point to a location outside the sandbox. Symlinks can be resolved using
  // `sapi::file_util::fileops::ReadLink()`.
  //
  // NOTE: Requires namespace support.
  PolicyBuilder& AddDirectory(absl::string_view path, bool is_ro = true);
  PolicyBuilder& AddDirectoryAt(absl::string_view outside,
                                absl::string_view inside, bool is_ro = true);
  // Same as `AddDirectory`, but no error is raised if the directory does not
  // exist.
  PolicyBuilder& AddDirectoryIfExists(absl::string_view path,
                                      bool is_ro = true);

  // Adds a tmpfs inside the namespace.
  //
  // This will also create parent directories inside the namespace if needed.
  //
  // NOTE: Requires namespace support.
  PolicyBuilder& AddTmpfs(absl::string_view inside, size_t size);

  // Allows unrestricted access to the network by *not* creating a network
  // namespace. This only disables the network namespace. To actually allow
  // networking, you would also need to allow networking syscalls. Calling this
  // function will enable use of namespaces.
  ABSL_DEPRECATED("Use Allow(sandbox2::UnrestrictedNetworking()) instead.")
  PolicyBuilder& AllowUnrestrictedNetworking();

  // Enables a shared network namespace for all sandboxees that are started by
  // the same forkserver.
  //
  // This results in sandboxed processes to run in the same shared network
  // namespace instead of creating a separate network namespace for each
  // sandboxed process started by the ForkServer process.
  //
  // NOTE: Requires namespace support.
  //
  // IMPORTANT: This is incompatible with AllowUnrestrictedNetworking.
  PolicyBuilder& UseForkServerSharedNetNs();

  // Enables the use of namespaces.
  //
  // Namespaces are enabled by default.
  // This is a no-op.
  ABSL_DEPRECATED("Namespaces are enabled by default; no need to call this")
  PolicyBuilder& EnableNamespaces() {
    if (!use_namespaces_) {
      SetError(absl::FailedPreconditionError(
          "Namespaces cannot be both disabled and enabled"));
      return *this;
    }
    requires_namespaces_ = true;
    return *this;
  }

  // Set hostname in the network namespace.
  //
  // The default hostname is "sandbox2".
  //
  // NOTE: Requires namespace support.
  //
  // IMPORTANT: This is incompatible with AllowUnrestrictedNetworking.
  PolicyBuilder& SetHostname(absl::string_view hostname);

  // Enables/disables stack trace collection on violations.
  //
  // NOTE: This is enabled by default.
  PolicyBuilder& CollectStacktracesOnViolation(bool enable);

  // Enables/disables stack trace collection on signals (e.g. crashes / killed
  // from a signal).
  //
  // NOTE: This is enabled by default.
  PolicyBuilder& CollectStacktracesOnSignal(bool enable);

  // Enables/disables stack trace collection on hitting a timeout.
  //
  // NOTE: This is enabled by default.
  PolicyBuilder& CollectStacktracesOnTimeout(bool enable);

  // Enables/disables stack trace collection on getting killed by the sandbox
  // monitor or the user.
  //
  // NOTE: This is disabled by default.
  PolicyBuilder& CollectStacktracesOnKill(bool enable);

  // Enables/disables stack trace collection on normal process exit.
  //
  // NOTE: This is disabled by default.
  PolicyBuilder& CollectStacktracesOnExit(bool enable);

  // Enables/disables stack trace collection for all threads.
  //
  // NOTE: This is disabled by default.
  PolicyBuilder& CollectAllThreadsStacktrace(bool enable);

  // Changes the default action to ALLOW.
  //
  // All syscalls not handled explicitly by the policy will thus be
  // allowed.
  //
  // IMPORTANT: Do not use in environments with untrusted code and/or data.
  PolicyBuilder& DefaultAction(AllowAllSyscalls);

  // Changes the default action to `SANDBOX2_TRACE`.
  //
  // All syscalls not handled explicitly by the policy will be passed off to
  // the `sandbox2::Notify` implementation given to the `sandbox2::Sandbox2`
  // instance.
  PolicyBuilder& DefaultAction(TraceAllSyscalls);

  ABSL_DEPRECATED("Use DefaultAction(sandbox2::AllowAllSyscalls()) instead")
  PolicyBuilder& DangerDefaultAllowAll();

  // Allows syscalls that are necessary for the NetworkProxyClient.
  PolicyBuilder& AddNetworkProxyPolicy();

  // Allows syscalls that are necessary for the NetworkProxyClient and
  // the NetworkProxyHandler.
  PolicyBuilder& AddNetworkProxyHandlerPolicy();

  // Makes root of the filesystem writeable
  // Not recommended
  //
  // NOTE: Requires namespace support.
  PolicyBuilder& SetRootWritable();

  // Changes mounts propagation from MS_PRIVATE to MS_SLAVE.
  //
  ABSL_DEPRECATED("Use Allow(sandbox2::MapExec()) instead")
  PolicyBuilder& DangerAllowMountPropagation();
  PolicyBuilder& Allow(MountPropagation);
  // Changes mounts propagation from MS_PRIVATE to MS_SHARED for a specific
  // mount.
  PolicyBuilder& Allow(MountPropagation, absl::string_view inside);

  // Mounts writeable mappings without MS_NOEXEC.
  PolicyBuilder& Allow(WriteExecutable);

  // Allows connections to this IP.
  PolicyBuilder& AllowIPv4(const std::string& ip_and_mask, uint32_t port = 0);
  PolicyBuilder& AllowIPv6(const std::string& ip_and_mask, uint32_t port = 0);

  // Returns the current status of the PolicyBuilder.
  absl::Status GetStatus() { return last_status_; }

  const Mounts& mounts() const { return mounts_; }

  // Returns the absolute path for the given `relative_path`.
  //
  // If `relative_path` is absolute, it will be returned as is and `base` will
  // be ignored.
  //
  // If `relative_path` is relative and `base` is not provided, it will be
  // resolved relative to the current working directory.
  //
  // If `relative_path` is relative and an absolute `base` is provided, it will
  // be resolved relative to `base`.
  //
  // If both, `relative_path` and `base` are relative, then first `base` will be
  // resolved relative to the current working directory, and then
  // `relative_path` will be resolved relative to `base`.
  //
  // In all cases where `relative_path` is relative, non-canonical paths will be
  // canonicalized and the result must be anchored to the base directory. If the
  // resulting path is outside the base directory, an error will be returned.
  //
  // On ERROR, such as `relative_path` is empty, an empty string is returned.
  static std::string AnchorPathAbsolute(absl::string_view relative_path,
                                        absl::string_view base = {});

 private:
  friend class PolicyBuilderPeer;  // For testing
  friend class StackTracePeer;

  // Similar to AddFile(At)/AddDirectory(At) but it won't force use of
  // namespaces - files will only be added to the namespace if it is not
  // disabled by the time of TryBuild().
  PolicyBuilder& AddFileIfNamespaced(absl::string_view path, bool is_ro = true);
  PolicyBuilder& AddFileAtIfNamespaced(absl::string_view outside,
                                       absl::string_view inside,
                                       bool is_ro = true);
  PolicyBuilder& AddDirectoryIfNamespaced(absl::string_view path,
                                          bool is_ro = true);
  PolicyBuilder& AddDirectoryAtIfNamespaced(absl::string_view outside,
                                            absl::string_view inside,
                                            bool is_ro = true);

  // Allows a limited version of madvise.
  PolicyBuilder& AllowLimitedMadvise();

  // Allows MADV_POPULATE_READ and MADV_POPULATE_WRITE.
  PolicyBuilder& AllowMadvisePopulate();

  // Traps instead of denying ptrace.
  PolicyBuilder& TrapPtrace();

  // Appends given policy at the end of the policy - decision taken by user
  // policy takes precedence.
  PolicyBuilder& OverridableAddPolicyOnSyscalls(
      absl::Span<const uint32_t> nums, absl::Span<const sock_filter> policy);

  // Appends code to block a specific syscall and setting errno at the end of
  // the policy - decision taken by user policy takes precedence.
  PolicyBuilder& OverridableBlockSyscallWithErrno(uint32_t num, int error);

  PolicyBuilder& SetMounts(Mounts mounts) {
    mounts_ = std::move(mounts);
    return *this;
  }

  std::vector<sock_filter> ResolveBpfFunc(BpfFunc f);

  // This function returns a PolicyBuilder so that we can use it in the status
  // macros.
  PolicyBuilder& SetError(const absl::Status& status);

  Mounts mounts_;
  bool use_namespaces_ = true;
  bool requires_namespaces_ = false;
  NetNsMode netns_mode_ = NETNS_MODE_UNSPECIFIED;
  bool allow_map_exec_ = true;
  bool allow_safe_bpf_ = false;
  bool allow_speculation_ = false;
  bool allow_mount_propagation_ = false;
  bool allow_write_executable_ = false;
  std::string hostname_ = std::string(kDefaultHostname);

  // Stack trace collection
  bool collect_stacktrace_on_violation_ = true;
  bool collect_stacktrace_on_signal_ = true;
  bool collect_stacktrace_on_timeout_ = true;
  bool collect_stacktrace_on_kill_ = false;
  bool collect_stacktrace_on_exit_ = false;
  bool collect_stacktraces_for_all_threads_ = false;

  // Seccomp fields
  std::vector<sock_filter> user_policy_;
  std::vector<sock_filter> overridable_policy_;
  std::optional<sock_filter> default_action_;
  bool user_policy_handles_bpf_ = false;
  bool user_policy_handles_ptrace_ = false;
  absl::flat_hash_set<uint32_t> handled_syscalls_;
  absl::flat_hash_set<uint32_t> allowed_syscalls_;
  absl::flat_hash_set<uint32_t> blocked_syscalls_;
  absl::flat_hash_set<uint32_t> custom_policy_syscalls_;

  // Error handling
  absl::Status last_status_ = absl::OkStatus();
  bool already_built_ = false;

  struct {
    bool static_startup = false;
    bool dynamic_startup = false;
    bool system_malloc = false;
    bool scudo_malloc = false;
    bool tcmalloc = false;
    bool llvm_sanitizers = false;
    bool llvm_coverage = false;
    bool limited_madvise = false;
    bool madvise_populate = false;
    bool mmap_without_exec = false;
    bool mprotect_without_exec = false;
    bool safe_fcntl = false;
    bool tcgets = false;
    bool slow_fences = false;
    bool fast_fences = false;
    bool getrlimit = false;
    bool getrandom = false;
    bool wipe_on_fork = false;
    bool log_forwarding = false;
    bool prctl_set_name = false;
    bool prctl_set_vma = false;
    bool pkey_mprotect_without_exec = false;
    bool shared_memory = false;
  } allowed_complex_;

  // List of allowed hosts
  absl::optional<AllowedHosts> allowed_hosts_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_POLICYBUILDER_H_
