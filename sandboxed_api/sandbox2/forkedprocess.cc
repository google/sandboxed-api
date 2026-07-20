// Copyright 2026 Google LLC
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

#include "sandboxed_api/sandbox2/forkedprocess.h"

#include <fcntl.h>
#include <linux/filter.h>
#include <linux/prctl.h>
#include <linux/seccomp.h>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "libcap/include/sys/capability.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/sandbox2/latency_stop_watch.h"
#include "sandboxed_api/sandbox2/mounts.h"
#include "sandboxed_api/sandbox2/namespace.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/sanitizer.h"
#include "sandboxed_api/sandbox2/setup_latency_breakdown.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/raw_logging.h"

namespace sandbox2 {
namespace {
using ::sapi::file_util::fileops::FDCloser;

struct Pipe {
  FDCloser read;
  FDCloser write;
};

Pipe CreatePipe() {
  int pfds[2];
  SAPI_RAW_PCHECK(pipe(pfds) == 0, "creating pipe");
  return {FDCloser(pfds[0]), FDCloser(pfds[1])};
}

void DropAllCapabilities() {
  auto caps = cap_init();
  SAPI_RAW_CHECK(cap_set_proc(caps) == 0, "while dropping capabilities");
  cap_free(caps);
}

ABSL_ATTRIBUTE_NORETURN void RunInitProcess(pid_t main_pid,
                                            FDCloser synchronization_fd,
                                            FDCloser status_fd,
                                            bool allow_speculation) {
  if (prctl(PR_SET_NAME, "S2-INIT-PROC", 0, 0, 0) != 0) {
    SAPI_RAW_PLOG(WARNING, "prctl(PR_SET_NAME, 'S2-INIT-PROC')");
  }

  // Clear SA_NOCLDWAIT.
  struct sigaction sa;
  sa.sa_handler = SIG_DFL;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  SAPI_RAW_CHECK(sigaction(SIGCHLD, &sa, nullptr) == 0,
                 "clearing SA_NOCLDWAIT");

  // Apply seccomp.
  std::vector<sock_filter> code = {
      LOAD_ARCH,
      JNE32(sandbox2::Syscall::GetHostAuditArch(), DENY),

      LOAD_SYSCALL_NR,
      SYSCALL(__NR_waitid, ALLOW),
      SYSCALL(__NR_exit, ALLOW),
      SYSCALL(__NR_close, ALLOW),
  };
  if (status_fd.get() >= 0) {
    code.insert(code.end(),
                {SYSCALL(__NR_getrusage, ALLOW), SYSCALL(__NR_write, ALLOW)});
  }
  code.push_back(DENY);

  struct sock_fprog prog{
      .len = static_cast<uint16_t>(code.size()),
      .filter = code.data(),
  };

  uint32_t seccomp_extra_flags = 0;
  if (allow_speculation) {
    seccomp_extra_flags |= SECCOMP_FILTER_FLAG_SPEC_ALLOW;
  }
  SAPI_RAW_CHECK(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0,
                 "Denying new privs");
  SAPI_RAW_CHECK(prctl(PR_SET_KEEPCAPS, 0) == 0, "Dropping caps");
  SAPI_RAW_CHECK(syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER,
                         SECCOMP_FILTER_FLAG_TSYNC | seccomp_extra_flags,
                         reinterpret_cast<uintptr_t>(&prog)) == 0,
                 "Enabling seccomp filter");
  if (!synchronization_fd.Close()) {
    _exit(1);
  }

  siginfo_t info;
  // Reap children.
  for (;;) {
    int rv = TEMP_FAILURE_RETRY(waitid(P_ALL, -1, &info, WEXITED | __WALL));
    if (rv != 0) {
      _exit(1);
    }

    if (info.si_pid == main_pid) {
      if (status_fd.get() >= 0) {
        (void)write(status_fd.get(), &info.si_code, sizeof(info.si_code));
        (void)write(status_fd.get(), &info.si_status, sizeof(info.si_status));

        rusage usage{};
        getrusage(RUSAGE_CHILDREN, &usage);
        (void)write(status_fd.get(), &usage, sizeof(usage));
      }
      _exit(0);
    }
  }
}

ABSL_ATTRIBUTE_NORETURN void ExecuteProcess(int execve_fd,
                                            const char* const* argv,
                                            const char* const* envp) {
  // Do not add any code before execve(), as it's subject to seccomp policies.
  // Indicate that it's a special execve(), by setting 4th, 5th and 6th syscall
  // argument to magic values.
  util::Execveat(execve_fd, "", argv, envp, AT_EMPTY_PATH,
                 internal::kExecveMagic);

  int saved_errno = errno;
  SAPI_RAW_PLOG(ERROR, "execveat failed");
  if (argv[0]) {
    SAPI_RAW_LOG(ERROR, "argv[0]=%s", argv[0]);
  }

  if (saved_errno == ENOSYS) {
    SAPI_RAW_LOG(ERROR,
                 "This is likely caused by running on a kernel that is too old."
    );
  } else if (saved_errno == ENOENT && execve_fd >= 0) {
    // Since we know the file exists, it must be that the file is dynamically
    // linked and the ELF interpreter is what's actually missing.
    SAPI_RAW_LOG(
        ERROR,
        "This is likely caused by running dynamically-linked sandboxee without "
        "calling .AddLibrariesForBinary() on the policy builder.");
  }

  util::Syscall(__NR_exit_group, EXIT_FAILURE);
  abort();
}

// "Moves" FDs in move_fds from current to target FD number - potentially moving
// them to another FD number as well in case of colisions.
// Ignores invalid (-1) fds.
void MoveFDs(std::initializer_list<std::pair<FDCloser*, int>> move_fds) {
  absl::flat_hash_map<int, FDCloser*> fd_map;
  for (auto [old_fd, new_fd] : move_fds) {
    if (old_fd->get() != -1) {
      fd_map.emplace(old_fd->get(), old_fd);
    }
  }

  for (auto [old_fd, new_fd] : move_fds) {
    if (old_fd->get() == -1 || old_fd->get() == new_fd) {
      continue;
    }

    // Make sure we won't override another fd
    if (auto it = fd_map.find(new_fd); it != fd_map.end()) {
      int fd = dup(new_fd);
      SAPI_RAW_CHECK(fd != -1, "Duplicating an FD failed.");
      *it->second = FDCloser(fd);
      fd_map.emplace(fd, it->second);
      fd_map.erase(it);
    }
    if (dup2(old_fd->get(), new_fd) == -1) {
      SAPI_RAW_PLOG(FATAL, "Moving temporary to proper FD failed.");
    }

    fd_map.erase(old_fd->get());
    *old_fd = FDCloser(new_fd);
  }
}

}  // namespace

void ForkedProcess::SanitizeEnvironment() {
  absl::Status status = sanitizer::SanitizeCurrentProcess();
  SAPI_RAW_CHECK(
      status.ok(),
      absl::StrCat("while sanitizing environment: ", status.message()).c_str());
}

FDCloser ForkedProcess::CreateAndSendStatusPipe() {
  Pipe pipe_fds = CreatePipe();
  SAPI_RAW_CHECK(setup_comms_.SendFD(pipe_fds.read.get()),
                 "Failed to send status pipe");
  pipe_fds.read.Close();
  return std::move(pipe_fds.write);
}

void ForkedProcess::LaunchInit() {
  Pipe sync_pipe = CreatePipe();
  FDCloser status_fd;
  if (request_.monitor_type() == FORKSERVER_MONITOR_UNOTIFY) {
    status_fd = CreateAndSendStatusPipe();
  }
  SAPI_RAW_CHECK(setup_comms_.SendCreds(), "Failed to send init_pid");
  pid_t sandboxee_pid;
  if (execve_fd_.get() >= 0) {
    sandboxee_pid = util::ForkWithFlags(SIGCHLD);
  } else {
    // Use regular fork() so that pthread's state is properly initialized in
    // the child process.
    sandboxee_pid = fork();
  }
  SAPI_RAW_PCHECK(sandboxee_pid != -1, "fork failed");
  if (sandboxee_pid == 0) {
    // Wait for init to finish setup before returning.
    sync_pipe.write.Close();
    char dummy = 0;
    SAPI_RAW_PCHECK(
        TEMP_FAILURE_RETRY(read(sync_pipe.read.get(), &dummy, 1)) == 0,
        "synchronizing with init process");
    return;
  }
  // Run init process
  DropAllCapabilities();
  sync_pipe.read.Close();
  sanitizer::CloseAllFDsExcept({sync_pipe.write.get(), status_fd.get()});
  RunInitProcess(sandboxee_pid, std::move(sync_pipe.write),
                 std::move(status_fd), request_.allow_speculation());
}

void ForkedProcess::PrepareExecveArgs() {
  // Prepare arguments for execve.
  for (const auto& arg : request_.args()) {
    args_.push_back(arg);
  }

  // Prepare environment variables for execve.
  for (const auto& env : request_.envs()) {
    envp_.push_back(env);
  }

  // The child process should not start any fork-servers.
  envp_.push_back(absl::StrCat(kForkServerDisableEnv, "=1"));

  constexpr char kSapiVlogLevel[] = "SAPI_VLOG_LEVEL";
  char* sapi_vlog = getenv(kSapiVlogLevel);
  if (sapi_vlog && strlen(sapi_vlog) > 0) {
    envp_.push_back(absl::StrCat(kSapiVlogLevel, "=", sapi_vlog));
  }

  SAPI_RAW_VLOG(1, "Will execute args:['%s'], environment:['%s']",
                absl::StrJoin(args_, "', '").c_str(),
                absl::StrJoin(envp_, "', '").c_str());
}

void ForkedProcess::MoveToPredefiedFDs() {
  MoveFDs({{&comms_fd_, Comms::kSandbox2ClientCommsFD},
           {&execve_fd_, Comms::kSandbox2TargetExecFD}});
}

void ForkedProcess::LaunchSandboxee() {
  const bool should_sandbox = request_.mode() == FORKSERVER_FORK_EXECVE_SANDBOX;

  // Prepare the arguments before sandboxing (if needed), as doing it after
  // sandoxing can cause syscall violations (e.g. related to memory management).
  MoveToPredefiedFDs();
  PrepareExecveArgs();

  sanitizer::CloseAllFDsExcept({STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO,
                                comms_fd_.get(), execve_fd_.get()});

  Comms comms(comms_fd_.Release());
  Client client(&comms);

  int execve_fd = execve_fd_.get();

  // Sandboxing can be enabled either here - just before execve, or somewhere
  // inside the executed binary (e.g. after basic structures have been
  // initialized, and resources acquired). In the latter case, it's up to the
  // sandboxed binary to establish proper Comms channel (using
  // Comms::kSandbox2ClientCommsFD) and call sandbox2::Client::SandboxMeHere()
  if (should_sandbox) {
    // The following client calls are basically SandboxMeHere. We split it so
    // that we can set up the envp after we received the file descriptors but
    // before we enable the syscall filter.
    client.PrepareEnvironment(&execve_fd);
    if (comms.GetConnectionFD() != Comms::kSandbox2ClientCommsFD) {
      envp_.push_back(absl::StrCat(Comms::kSandbox2CommsFDEnvVar, "=",
                                   comms.GetConnectionFD()));
    }
    envp_.push_back(client.GetFdMapEnvVar());
  }

  // Make sure the execve_fd is CLOEXEC after the moves.
  SAPI_RAW_PCHECK(fcntl(execve_fd, F_SETFD, FD_CLOEXEC) == 0,
                  "setting execve_fd to be CLOEXEC");

  // Convert args and envs before enabling sandbox (it'll allocate which might
  // be blocked).
  util::CharPtrArray argv = util::CharPtrArray::FromStringVector(args_);
  util::CharPtrArray envp = util::CharPtrArray::FromStringVector(envp_);

  if (should_sandbox) {
    client.EnableSandbox();
  }

  ExecuteProcess(execve_fd, argv.data(), envp.data());
}

void ForkedProcess::JoinInitialUserNamespace() {
  FDCloser initial_userns_fd;
  SAPI_RAW_CHECK(setup_comms_.RecvFD(&initial_userns_fd),
                 "Failed to receive initial user namespace FD");
  SAPI_RAW_PCHECK(setns(initial_userns_fd.get(), CLONE_NEWUSER) == 0,
                  "joining initial user namespace");
}

void ForkedProcess::JoinSharedPidNamespace() {
  FDCloser shared_pidns_fd;
  SAPI_RAW_CHECK(setup_comms_.RecvFD(&shared_pidns_fd),
                 "Failed to receive initial pid namespace FD");
  SAPI_RAW_PCHECK(setns(shared_pidns_fd.get(), CLONE_NEWPID) == 0,
                  "joining initial pid namespace");
  pid_t pid = util::ForkWithFlags(CLONE_PARENT | SIGCHLD);
  if (pid != 0) {
    // We'll continue just in the child.
    _exit(0);
  }
  SAPI_RAW_PCHECK(pid != -1, "fork failed");
  latency_breakdown_.SetLatency(SetupLatencyBreakdown::kSharedPidInitFork,
                                latency_stop_watch_.LapTime());
}

void ForkedProcess::JoinMountNamespace() {
  FDCloser mntns_fd;
  SAPI_RAW_CHECK(setup_comms_.RecvFD(&mntns_fd),
                 "Failed to receive mount namespace FD");
  SAPI_RAW_PCHECK(setns(mntns_fd.get(), CLONE_NEWNS) == 0,
                  "joining mount namespace");
}

void ForkedProcess::JoinNetworkNamespace() {
  FDCloser shared_netns_fd;
  SAPI_RAW_CHECK(setup_comms_.RecvFD(&shared_netns_fd),
                 "Failed to receive shared network namespace FD");
  SAPI_RAW_PCHECK(setns(shared_netns_fd.get(), CLONE_NEWNET) == 0,
                  "joining shared network namespace");
}

void ForkedProcess::EnforceIsolation(FDCloser proc_self_fd, uid_t uid,
                                     gid_t gid) {
  if (request_.use_landlock()) {
    Namespace::EnforceLandlockIsolation(request_.clone_flags() | CLONE_NEWUSER,
                                        Mounts(request_.mount_specs()), uid,
                                        gid, latency_breakdown_);

    // TODO(cffsmith): If UnotifyMonitor support is added back to Landlock mode,
    // we will need to launch an init process here to coordinate return values
    // (similar to standard mode's LaunchInit()). Remember to also define and
    // add a respective setup latency measurement at that point.
    return;
  }

  if (request_.mount_specs().use_shared_mount_namespace()) {
    SAPI_RAW_PCHECK(unshare(CLONE_NEWUSER) == 0, "unsharing user namespace");
    Namespace::SetupIDMaps(proc_self_fd.get(), uid, gid);
    return;
  }

  Namespace::InitializeNamespaces(uid, gid, request_, latency_breakdown_);
  latency_breakdown_.SetLatency(
      SetupLatencyBreakdown::kNamespacesInitialization,
      latency_stop_watch_.LapTime());
}

void ForkedProcess::SetupNamespaces() {
  JoinInitialUserNamespace();
  const bool use_shared_pidns =
      request_.mount_specs().use_shared_mount_namespace() ||
      request_.use_landlock();
  if (use_shared_pidns) {
    JoinSharedPidNamespace();
  }
  const uid_t uid = getuid();
  const gid_t gid = getgid();
  const bool has_newpid = request_.clone_flags() & CLONE_NEWPID;
  if (has_newpid) {
    // Cannot use regular fork because we need to be single-threaded to setns.
    // This won't be the case with some sanitizers (e.g. TSAN).
    // We also need CLONE_PARENT for PR_SET_PDEATHSIG to work as expected.
    pid_t pid = util::ForkWithFlags(CLONE_NEWPID | CLONE_PARENT | SIGCHLD);
    SAPI_RAW_PCHECK(pid != -1, "fork failed");
    if (pid != 0) {
      // We'll continue just in the child.
      _exit(0);
    }
    latency_breakdown_.SetLatency(SetupLatencyBreakdown::kInitFork,
                                  latency_stop_watch_.LapTime());
    SanitizeEnvironment();
  }
  FDCloser proc_self_fd(TEMP_FAILURE_RETRY(open("/proc/self", O_PATH)));
  JoinMountNamespace();
  int32_t unshare_flags =
      request_.clone_flags() &
      (CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWNET | CLONE_NEWUTS);
  if (request_.netns_mode() == NETNS_MODE_SHARED_PER_FORKSERVER) {
    unshare_flags &= ~CLONE_NEWNET;
    JoinNetworkNamespace();
  }
  latency_breakdown_.SetLatency(SetupLatencyBreakdown::kTillNamespacesUnshare,
                                latency_stop_watch_.LapTime());
  SAPI_RAW_PCHECK(unshare(unshare_flags) == 0, "unsharing namespaces");
  latency_breakdown_.SetLatency(SetupLatencyBreakdown::kNamespacesUnshare,
                                latency_stop_watch_.LapTime());
  EnforceIsolation(std::move(proc_self_fd), uid, gid);
  if (has_newpid) {
    LaunchInit();
    latency_breakdown_.SetLatency(SetupLatencyBreakdown::kInitLaunch,
                                  latency_stop_watch_.LapTime());
  }
}

void ForkedProcess::ReceiveFDs(bool will_exec) {
  SAPI_RAW_CHECK(setup_comms_.RecvFD(&comms_fd_), "Failed to receive Comms FD");
  if (will_exec) {
    SAPI_RAW_CHECK(setup_comms_.RecvFD(&execve_fd_),
                   "Failed to receive Exec FD");
  }
}

Comms ForkedProcess::Setup() {
  // Restore the default handler for SIGTERM.
  if (signal(SIGTERM, SIG_DFL) == SIG_ERR) {
    SAPI_RAW_PLOG(WARNING, "signal(SIGTERM, SIG_DFL)");
  }

  const bool has_namespaces = request_.clone_flags() & CLONE_NEWUSER;
  const bool will_exec = request_.mode() == FORKSERVER_FORK_EXECVE ||
                         request_.mode() == FORKSERVER_FORK_EXECVE_SANDBOX;

  ReceiveFDs(will_exec);
  SanitizeEnvironment();
  if (has_namespaces) {
    SetupNamespaces();
  }
  DropAllCapabilities();
  // Send sandboxee pid.
  SAPI_RAW_CHECK(setup_comms_.SendCreds(), "Failed to send sandboxee_pid");
  latency_breakdown_.SetLatency(SetupLatencyBreakdown::kTillAlmostDone,
                                latency_stop_watch_.LapTime());
  latency_breakdown_.Send(setup_comms_);
  setup_comms_.Terminate();
  if (will_exec) {
    LaunchSandboxee();
  }
  return Comms(comms_fd_.Release());
}
}  // namespace sandbox2
