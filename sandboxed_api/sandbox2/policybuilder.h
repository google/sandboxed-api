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
#include <functional>
#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "absl/base/macros.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "sandboxed_api/sandbox2/mounts.h"
#include "sandboxed_api/sandbox2/network_proxy/filtering.h"
#include "sandboxed_api/sandbox2/policy.h"

struct bpf_labels;

namespace sandbox2 {

// PolicyBuilder is a helper class to simplify creation of policies. The builder
// uses fluent interface for convenience and increased readability of policies.
//
// To build a policy you simply create a new builder object, call methods on it
// specifying what you want and finally call BuildOrDie() to generate you
// policy.
//
// For instance this would generate a simple policy suitable for binaries doing
// only computations:
//
// std::unique_ptr<Policy> policy =
//     PolicyBuilder()
//       .AllowRead()
//       .AllowWrite()
//       .AllowExit()
//       .AllowSystemMalloc()
//       .BuildOrDie();
//
// Note that operations are executed in the order they are dictated, though in
// most cases this has no influence since the operations themselves commute.
//
// For instance these two policies are equivalent:
//
// auto policy = PolicyBuilder.AllowRead().AllowWrite().BuildOrDie();
// auto policy = PolicyBuilder.AllowWrite().AllowRead().BuildOrDie();
//
// While these two are not:
//
// auto policy = PolicyBuilder.AllowRead().BlockSyscallWithErrno(__NR_read, EIO)
//                            .BuildOrDie();
// auto policy = PolicyBuilder.BlockSyscallWithErrno(__NR_read, EIO).AllowRead()
//                            .BuildOrDie();
//
// In fact the first one is equivalent to:
//
// auto policy = PolicyBuilder.AllowRead().BuildOrDie();
//
// If you dislike the chained style, it is also possible to write the first
// example as this:
//
// PolicyBuilder builder;
// builder.AllowRead();
// builder.AllowWrite();
// builder.AllowExit();
// builder.AllowSystemMalloc();
// auto policy = builder.BuildOrDie();
//
// For a more complicated example, see examples/persistent/persistent_sandbox.cc
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
  // We set it lower so that there is for sure some room for the default policy.
  static constexpr size_t kMaxUserPolicyLength = 30000;

  using BpfFunc = const std::function<std::vector<sock_filter>(bpf_labels&)>&;

  // Appends code to allow a specific syscall
  PolicyBuilder& AllowSyscall(uint32_t num);

  // Appends code to allow a number of syscalls
  PolicyBuilder& AllowSyscalls(absl::Span<const uint32_t> nums);

  // Appends code to block a syscalls while setting errno to the error given.
  PolicyBuilder& BlockSyscallsWithErrno(absl::Span<const uint32_t> nums,
                                        int error);

  // Appends code to block a specific syscall and setting errno.
  PolicyBuilder& BlockSyscallWithErrno(uint32_t num, int error);

  // Appends code to allow using epoll.
  // Allows these syscalls:
  // - epoll_create
  // - epoll_create1
  // - epoll_ctl
  // - epoll_wait
  // - epoll_pwait
  // - epoll_pwait2
  PolicyBuilder& AllowEpoll();

  // Appends code to allow exiting.
  // Allows these syscalls:
  // - exit
  // - exit_group
  PolicyBuilder& AllowExit();

  // Appends code to allow restartable sequences and necessary /proc files.
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
  //
  // This function enables namespaces! If your policy disables namespaces,
  // the conflict will cause an error when the policy is built. You should
  // call AllowRestartableSequences() instead; see below for instructions.
  PolicyBuilder& AllowRestartableSequencesWithProcFiles(
      CpuFenceMode cpu_fence_mode);

  // Appends code to allow restartable sequences.
  // See above for the allowed syscalls and, more importantly, for the files
  // that you are responsible for allowing via the deprecated `Fs` mechanism.
  PolicyBuilder& AllowRestartableSequences(CpuFenceMode cpu_fence_mode);

  // Appends code to allow the scudo version of malloc, free and
  // friends. This should be used in conjunction with namespaces. If scudo
  // options are passed to the sandboxee through an environment variable, access
  // to "/proc/self/environ" will have to be allowed by the policy.
  //
  // Note: This function is tuned towards the secure scudo allocator. If you are
  //       using another implementation, this function might not be the most
  //       suitable.
  PolicyBuilder& AllowScudoMalloc();

  // Appends code to allow the system-allocator version of malloc, free and
  // friends.
  //
  // Note: This function is tuned towards the malloc implementation in glibc. If
  //       you are using another implementation, this function might not be the
  //       most suitable.
  PolicyBuilder& AllowSystemMalloc();

  // Appends code to allow the tcmalloc version of malloc, free and
  // friends.
  PolicyBuilder& AllowTcMalloc();

  // Allows system calls typically used by the LLVM sanitizers (address
  // sanitizer, memory sanitizer, and thread sanitizer). This method is
  // intended as a best effort for adding system calls that are common to many
  // binaries. It may not be fully inclusive of all potential system calls for
  // all binaries.
  PolicyBuilder& AllowLlvmSanitizers();

  // Appends code to allow mmap. Specifically this allows mmap and mmap2 syscall
  // on architectures where this syscalls exist.
  PolicyBuilder& AllowMmap();

  // Appends code to allow calling futex with the given operation.
  PolicyBuilder& AllowFutexOp(int op);

  // Appends code to allow opening files or directories. Specifically it allows
  // these sycalls:
  //
  // - open
  // - openat
  PolicyBuilder& AllowOpen();

  // Appends code to allow calling stat, fstat and lstat.
  // Allows these sycalls:
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
  // Allows these syscalls:
  // - access
  // - faccessat
  PolicyBuilder& AllowAccess();

  // Appends code to the policy to allow reading from file descriptors.
  // Allows these sycalls:
  // - read
  // - readv
  // - preadv
  // - pread64
  PolicyBuilder& AllowRead();

  // Appends code to the policy to allow writing to file descriptors.
  // Allows these sycalls:
  // - write
  // - writev
  // - pwritev
  // - pwrite64
  PolicyBuilder& AllowWrite();

  // Appends code to allow reading directories.
  // Allows these sycalls:
  // - getdents
  // - getdents64
  PolicyBuilder& AllowReaddir();

  // Appends code to allow safe calls to fcntl.
  // Allows these sycalls:
  // - fcntl
  // - fcntl64 (on architectures where it exists)
  //
  // The above are only allowed when the cmd is one of:
  // F_GETFD, F_SETFD, F_GETFL, F_SETFL, F_GETLK, F_SETLKW, F_SETLK,
  // F_DUPFD, F_DUPFD_CLOEXEC
  PolicyBuilder& AllowSafeFcntl();

  // Appends code to allow creating new processes.
  // Allows these sycalls:
  // - fork
  // - vfork
  // - clone
  //
  // Note: while this function allows the calls, the default policy is run first
  // and it has checks for dangerous flags which can create a violation. See
  // sandbox2/policy.cc for more details.
  PolicyBuilder& AllowFork();

  // Appends code to allow waiting for processes.
  // Allows these sycalls:
  // - waitpid (on architectures where it exists)
  // - wait4
  PolicyBuilder& AllowWait();

  // Appends code to allow setting up signal handlers, returning from them, etc.
  // Allows these sycalls:
  // - rt_sigaction
  // - rt_sigreturn
  // - rt_procmask
  // - signal (on architectures where it exists)
  // - sigaction (on architectures where it exists)
  // - sigreturn (on architectures where it exists)
  // - sigprocmask (on architectures where it exists)
  PolicyBuilder& AllowHandleSignals();

  // Appends code to allow doing the TCGETS ioctl.
  // Allows these sycalls:
  // - ioctl (when the first argument is TCGETS)
  PolicyBuilder& AllowTCGETS();

  // Appends code to allow to getting the current time.
  // Allows these sycalls:
  // - time
  // - gettimeofday
  // - clock_gettime
  PolicyBuilder& AllowTime();

  // Appends code to allow sleeping in the current thread.
  // Allow these syscalls:
  // - clock_nanosleep
  // - nanosleep
  PolicyBuilder& AllowSleep();

  // Appends code to allow getting the uid, euid, gid, etc.
  // - getuid + geteuid + getresuid
  // - getgid + getegid + getresgid
  // - getuid32 + geteuid32 + getresuid32 (on architectures where they exist)
  // - getgid32 + getegid32 + getresgid32 (on architectures where they exist)
  // - getgroups
  PolicyBuilder& AllowGetIDs();

  // Appends code to allow getting the pid, ppid and tid.
  // Allows these syscalls:
  // - getpid
  // - getppid
  // - gettid
  PolicyBuilder& AllowGetPIDs();

  // Appends code to allow getting the rlimits.
  // Allows these sycalls:
  // - getrlimit
  // - ugetrlimit (on architectures where it exist)
  PolicyBuilder& AllowGetRlimit();

  // Appends code to allow setting the rlimits.
  // Allows these sycalls:
  // - setrlimit
  // - usetrlimit (on architectures where it exist)
  PolicyBuilder& AllowSetRlimit();

  // Appends code to allow reading random bytes.
  // Allows these sycalls:
  // - getrandom (with no flags or GRND_NONBLOCK)
  //
  PolicyBuilder& AllowGetRandom();

  // Appends code to allow configuring wipe-on-fork memory
  // Allows these syscalls:
  // - madvise (with advice equal to -1 or MADV_WIPEONFORK).
  PolicyBuilder& AllowWipeOnFork();

  // Enables syscalls required to use the logging support enabled via
  // Client::SendLogsToSupervisor()
  // Allows the following:
  // - Writes
  // - kill(0, SIGABRT) (for LOG(FATAL))
  // - clock_gettime
  // - gettid
  // - close
  PolicyBuilder& AllowLogForwarding();

  // Appends code to allow deleting files
  // Allows these syscalls:
  // - unlink (if available)
  // - unlinkat
  PolicyBuilder& AllowUnlink();

  // Appends code to allow renaming files
  // Allows these syscalls:
  // - rename (if available)
  // - renameat
  // - renameat2
  PolicyBuilder& AllowRename();

  // Appends code to allow setting the name of a thread
  // Allows the following
  // - prctl(PR_SET_NAME, ...)
  PolicyBuilder& AllowPrctlSetName();

  // Enables the syscalls necessary to start a statically linked binary
  //
  // NOTE: This will call BlockSyscallWithErrno(__NR_readlink, ENOENT). If you
  // do not want readlink blocked, put a different call before this call.
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
  // Additionally it will block calls to readlink.
  PolicyBuilder& AllowStaticStartup();

  // In addition to syscalls allowed by AllowStaticStartup, also allow reading,
  // seeking, mmapping and closing files. It does not allow opening them, as
  // the mechanism for doing so depends on whether GetFs-checks are used or not.
  PolicyBuilder& AllowDynamicStartup();

  // Appends a policy, which will be run on the specified syscall.
  // This policy must be written without labels. If you need labels, use
  // the overloaded function passing a BpfFunc object instead of the
  // sock_filter.
  PolicyBuilder& AddPolicyOnSyscall(uint32_t num,
                                    absl::Span<const sock_filter> policy);

  // Appends a policy, which will be run on the specified syscall.
  // This policy may use labels.
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
  PolicyBuilder& AddPolicyOnSyscall(uint32_t num, BpfFunc f);

  // Appends a policy, which will be run on the specified syscalls.
  // This policy must be written without labels.
  PolicyBuilder& AddPolicyOnSyscalls(absl::Span<const uint32_t> nums,
                                     absl::Span<const sock_filter> policy);

  // Appends a policy, which will be run on the specified syscalls.
  // This policy may use labels.
  PolicyBuilder& AddPolicyOnSyscalls(absl::Span<const uint32_t> nums,
                                     BpfFunc f);

  // Equivalent to AddPolicyOnSyscalls(mmap_syscalls, policy), where
  // mmap_syscalls is a subset of {__NR_mmap, __NR_mmap2}, which exists on the
  // target architecture.
  PolicyBuilder& AddPolicyOnMmap(absl::Span<const sock_filter> policy);

  // Equivalent to AddPolicyOnSyscalls(mmap_syscalls, f), where mmap_syscalls
  // is a subset of {__NR_mmap, __NR_mmap2}, which exists on the target
  // architecture.
  PolicyBuilder& AddPolicyOnMmap(BpfFunc f);

  // Builds the policy returning a unique_ptr to it. This should only be
  // called once.
  absl::StatusOr<std::unique_ptr<Policy>> TryBuild();

  // Builds the policy returning a unique_ptr to it. This should only be
  // called once. This function will abort if an error happened in any of the
  // PolicyBuilder methods.
  std::unique_ptr<Policy> BuildOrDie() { return TryBuild().value(); }

  // Adds a bind-mount for a file from outside the namespace to inside. This
  // will also create parent directories inside the namespace if needed.
  //
  // Calling these function will enable use of namespaces.
  PolicyBuilder& AddFile(absl::string_view path, bool is_ro = true);
  PolicyBuilder& AddFileAt(absl::string_view outside, absl::string_view inside,
                           bool is_ro = true);

  // Best-effort function that adds the libraries and linker required by a
  // binary.
  //
  // This does not add the binary itself, only the libraries it depends on.
  //
  // This function should work correctly for most binaries, but you might need
  // to tweak it in some cases.
  //
  // This function is safe even for untrusted/potentially malicious binaries.
  // It adds libraries only from standard library dirs and ld_library_path.
  //
  // run `ldd` yourself and use AddFile or AddDirectory.
  PolicyBuilder& AddLibrariesForBinary(absl::string_view path,
                                       absl::string_view ld_library_path = {});

  // Similar to AddLibrariesForBinary, but binary is specified with an open
  // fd.
  PolicyBuilder& AddLibrariesForBinary(int fd,
                                       absl::string_view ld_library_path = {});

  // Adds a bind-mount for a directory from outside the namespace to
  // inside.  This will also create parent directories inside the namespace if
  // needed.
  //
  // Calling these function will enable use of namespaces.
  PolicyBuilder& AddDirectory(absl::string_view path, bool is_ro = true);
  PolicyBuilder& AddDirectoryAt(absl::string_view outside,
                                absl::string_view inside, bool is_ro = true);

  // Adds a tmpfs inside the namespace. This will also create parent
  // directories inside the namespace if needed.
  //
  // Calling this function will enable use of namespaces.
  PolicyBuilder& AddTmpfs(absl::string_view inside, size_t size);

  // Allows unrestricted access to the network by *not* creating a network
  // namespace. Note that this only disables the network namespace. To
  // actually allow networking, you would also need to allow networking
  // syscalls. Calling this function will enable use of namespaces.
  PolicyBuilder& AllowUnrestrictedNetworking();

  // Enables the use of namespaces.
  //
  // Namespaces are enabled by default.
  // This is a no-op.
  ABSL_DEPRECATED("Namespaces are enabled by default; no need to call this")
  PolicyBuilder& EnableNamespaces() {
    CHECK(use_namespaces_) << "Namespaces cannot be both disabled and enabled";
    requires_namespaces_ = true;
    return *this;
  }

  // Disables the use of namespaces.
  //
  // Call in order to use Sandbox2 without namespaces.
  // This is not recommended.
  PolicyBuilder& DisableNamespaces() {
    CHECK(!requires_namespaces_)
        << "Namespaces cannot be both disabled and enabled. You're probably "
           "using features that implicitly enable namespaces (SetHostname, "
           "AddFile, AddDirectory, AddDataDependency, AddLibrariesForBinary "
           "or similar)";
    use_namespaces_ = false;
    return *this;
  }

  // Set hostname in the network namespace instead of default "sandbox2".
  //
  // Calling this function will enable use of namespaces.
  // It is an error to also call AllowUnrestrictedNetworking.
  PolicyBuilder& SetHostname(absl::string_view hostname);

  // Enables/disables stack trace collection on violations.
  PolicyBuilder& CollectStacktracesOnViolation(bool enable);

  // Enables/disables stack trace collection on signals (e.g. crashes / killed
  // from a signal).
  PolicyBuilder& CollectStacktracesOnSignal(bool enable);

  // Enables/disables stack trace collection on hitting a timeout.
  PolicyBuilder& CollectStacktracesOnTimeout(bool enable);

  // Enables/disables stack trace collection on getting killed by the sandbox
  // monitor / the user.
  PolicyBuilder& CollectStacktracesOnKill(bool enable);

  // Enables/disables stack trace collection on normal process exit.
  PolicyBuilder& CollectStacktracesOnExit(bool enable);

  // Appends an unconditional ALLOW action for all syscalls.
  // Do not use in environment with untrusted code and/or data, ask
  // sandbox-team@ first if unsure.
  PolicyBuilder& DangerDefaultAllowAll();

  // Allows syscalls that are necessary for the NetworkProxyClient
  PolicyBuilder& AddNetworkProxyPolicy();

  // Allows syscalls that are necessary for the NetworkProxyClient and
  // the NetworkProxyHandler
  PolicyBuilder& AddNetworkProxyHandlerPolicy();

  // Makes root of the filesystem writeable
  // Not recommended
  PolicyBuilder& SetRootWritable();

  // Changes mounts propagation from MS_PRIVATE to MS_SLAVE.
  PolicyBuilder& DangerAllowMountPropagation() {
    allow_mount_propagation_ = true;
    return *this;
  }

  // Allows connections to this IP.
  PolicyBuilder& AllowIPv4(const std::string& ip_and_mask, uint32_t port = 0);
  PolicyBuilder& AllowIPv6(const std::string& ip_and_mask, uint32_t port = 0);

 private:
  friend class PolicyBuilderPeer;  // For testing
  friend class StackTracePeer;

  static absl::StatusOr<std::string> ValidateAbsolutePath(
      absl::string_view path);
  static absl::StatusOr<std::string> ValidatePath(absl::string_view path);

  // Allows a limited version of madvise
  PolicyBuilder& AllowLimitedMadvise();

  // Traps instead of denying ptrace.
  PolicyBuilder& TrapPtrace();

  // Appends code to block a specific syscall and setting errno at the end of
  // the policy - decision taken by user policy take precedence.
  PolicyBuilder& OverridableBlockSyscallWithErrno(uint32_t num, int error);

  PolicyBuilder& SetMounts(Mounts mounts) {
    mounts_ = std::move(mounts);
    return *this;
  }

  std::vector<sock_filter> ResolveBpfFunc(BpfFunc f);

  void StoreDescription(PolicyBuilderDescription* pb_description);

  // This function returns a PolicyBuilder so that we can use it in the status
  // macros
  PolicyBuilder& SetError(const absl::Status& status);

  Mounts mounts_;
  bool use_namespaces_ = true;
  bool requires_namespaces_ = false;
  bool allow_unrestricted_networking_ = false;
  bool allow_mount_propagation_ = false;
  std::string hostname_ = std::string(kDefaultHostname);

  bool collect_stacktrace_on_violation_ = true;
  bool collect_stacktrace_on_signal_ = true;
  bool collect_stacktrace_on_timeout_ = true;
  bool collect_stacktrace_on_kill_ = false;
  bool collect_stacktrace_on_exit_ = false;

  // Seccomp fields
  std::vector<sock_filter> user_policy_;
  std::vector<sock_filter> overridable_policy_;
  bool user_policy_handles_bpf_ = false;
  bool user_policy_handles_ptrace_ = false;
  absl::flat_hash_set<uint32_t> handled_syscalls_;

  // Error handling
  absl::Status last_status_;
  bool already_built_ = false;

  // Contains list of allowed hosts.
  absl::optional<AllowedHosts> allowed_hosts_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_POLICYBUILDER_H_
