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
#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/mounts.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/stack-trace.h"
#include "sandboxed_api/util/statusor.h"

struct bpf_labels;

namespace sandbox2 {

constexpr char kDefaultHostname[] = "sandbox2";

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
// If you dislike the chained style, is is also possible to write the first
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
  using BpfInitializer = std::initializer_list<sock_filter>;
  using BpfFunc = const std::function<std::vector<sock_filter>(bpf_labels&)>&;
  using SyscallInitializer = std::initializer_list<unsigned int>;

  PolicyBuilder() : output_{new Policy()} {}

  PolicyBuilder(const PolicyBuilder&) = delete;
  PolicyBuilder& operator=(const PolicyBuilder&) = delete;

  // Appends code to allow a specific syscall
  PolicyBuilder& AllowSyscall(unsigned int num);

  // Appends code to allow a number of syscalls
  PolicyBuilder& AllowSyscalls(const std::vector<uint32_t>& nums);
  PolicyBuilder& AllowSyscalls(SyscallInitializer nums);

  // Appends code to block a specific syscall while setting errno to the error
  // given
  PolicyBuilder& BlockSyscallWithErrno(unsigned int num, int error);

  // Appends code to allow exiting.
  // Allows these syscalls:
  // - exit
  // - exit_group
  PolicyBuilder& AllowExit();

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

  // Appends code to allow mmap. Specifically this allows the mmap2 syscall on
  // architectures where this syscalls exist and the mmap syscall on all other
  // architectures.
  //
  // Note: while this function allows the calls, the default policy is run first
  // and it has checks for dangerous flags which can create a violation. See
  // sandbox2/policy.cc for more details.
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
  PolicyBuilder& AllowGetRandom();

  // Enables syscalls required to use the logging support enabled via
  // Client::SendLogsToSupervisor()
  // Allows the following:
  // - Writes
  // - kill(0, SIGABRT) (for LOG(FATAL))
  // - clock_gettime
  // - gettid
  // - close
  //
  // If you don't use namespaces you should also add this to your policy:
  // - policy->GetFs()->EnableSyscall(__NR_open);
  // - policy->GetFs()->AddRegexpToGreyList("/usr/share/zoneinfo/.*");
  PolicyBuilder& AllowLogForwarding();

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
  // This policy must be written without labels. If you need labels, use the
  // next function.
  PolicyBuilder& AddPolicyOnSyscall(unsigned int num, BpfInitializer policy);
  PolicyBuilder& AddPolicyOnSyscall(unsigned int num,
                                    const std::vector<sock_filter>& policy);

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
  PolicyBuilder& AddPolicyOnSyscall(unsigned int num, BpfFunc f);

  // Appends a policy, which will be run on the specified syscalls.
  // This policy must be written without labels.
  PolicyBuilder& AddPolicyOnSyscalls(SyscallInitializer nums,
                                     BpfInitializer policy);
  PolicyBuilder& AddPolicyOnSyscalls(SyscallInitializer nums,
                                     const std::vector<sock_filter>& policy);

  // Appends a policy, which will be run on the specified syscalls.
  // This policy may use labels.
  PolicyBuilder& AddPolicyOnSyscalls(SyscallInitializer nums, BpfFunc f);

  // Equivalent to AddPolicyOnSyscall(mmap_syscall_no, policy), where
  // mmap_syscall_no is either __NR_mmap or __NR_mmap2.
  PolicyBuilder& AddPolicyOnMmap(BpfInitializer policy);
  PolicyBuilder& AddPolicyOnMmap(const std::vector<sock_filter>& policy);

  // Equivalent to AddPolicyOnSyscall(mmap_syscall_no, f), where
  // mmap_syscall_no is either __NR_mmap or __NR_mmap2.
  PolicyBuilder& AddPolicyOnMmap(BpfFunc f);

  // Builds the policy returning a unique_ptr to it. This should only be called
  // once.
  ::sapi::StatusOr<std::unique_ptr<Policy>> TryBuild();

  // Builds the policy returning a unique_ptr to it. This should only be called
  // once.
  // This function will abort if an error happened in any off the PolicyBuilder
  // methods.
  std::unique_ptr<Policy> BuildOrDie() { return TryBuild().ValueOrDie(); }

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

  // Similar to AddLibrariesForBinary, but binary is specified with an open fd.
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
  PolicyBuilder& AddTmpfs(absl::string_view inside,
                          size_t sz = 4 << 20 /* 4MiB */);

  // Allows unrestricted access to the network by *not* creating a network
  // namespace. Note that this only disables the network namespace. To actually
  // allow networking, you would also need to allow networking syscalls.
  // Calling this function will enable use of namespaces.
  PolicyBuilder& AllowUnrestrictedNetworking();

  // Enables the use of namespaces.
  //
  // Namespaces are automatically enabled when using namespace helper features
  // (e.g. AddFile), therefore it is only necessary to explicitly enable
  // namespaces when not using any other namespace helper feature.
  PolicyBuilder& EnableNamespaces() {
    use_namespaces_ = true;
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

  // Appends an unconditional ALLOW action for all syscalls.
  // Do not use in environment with untrusted code and/or data, ask
  // sandbox-team@ first if unsure.
  PolicyBuilder& DangerDefaultAllowAll();

 private:
  friend class PolicyBuilderPeer;  // For testing
  friend class StackTracePeer;

  // Allows a limited version of madvise
  PolicyBuilder& AllowLimitedMadvise();

  PolicyBuilder& SetMounts(Mounts mounts) {
    mounts_ = std::move(mounts);
    return *this;
  }

  std::vector<sock_filter> ResolveBpfFunc(BpfFunc f);

  static ::sapi::StatusOr<std::string> ValidateAbsolutePath(absl::string_view path);
  static ::sapi::StatusOr<std::string> ValidatePath(absl::string_view path);

  void StoreDescription(PolicyBuilderDescription* pb_description);

  Mounts mounts_;
  bool use_namespaces_ = false;
  bool allow_unrestricted_networking_ = false;
  std::string hostname_ = kDefaultHostname;

  bool collect_stacktrace_on_violation_ = true;
  bool collect_stacktrace_on_signal_ = true;
  bool collect_stacktrace_on_timeout_ = true;
  bool collect_stacktrace_on_kill_ = false;

  // Seccomp fields
  std::unique_ptr<Policy> output_;
  std::set<unsigned int> handled_syscalls_;

  // Error handling
  ::sapi::Status last_status_;
  // This function returns a PolicyBuilder so that we can use it in the status
  // macros
  PolicyBuilder& SetError(const ::sapi::Status& status);
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_POLICYBUILDER_H_
