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

// Implementation of the sandbox2::ForkServer class.

#include "sandboxed_api/sandbox2/forkserver.h"

#include <fcntl.h>
#include <linux/filter.h>
#include <linux/prctl.h>
#include <linux/seccomp.h>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "libcap/include/sys/capability.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/fork_client.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/sandbox2/namespace.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/sanitizer.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/strerror.h"

namespace sandbox2 {
namespace {

using ::sapi::StrError;
using ::sapi::file_util::fileops::FDCloser;

// "Moves" FDs in move_fds from current to target FD number while keeping FDs
// in keep_fds open - potentially moving them to another FD number as well in
// case of colisions.
// Ignores invalid (-1) fds.
void MoveFDs(std::initializer_list<std::pair<int*, int>> move_fds,
             std::initializer_list<int*> keep_fds) {
  absl::flat_hash_map<int, int*> fd_map;
  for (int* fd : keep_fds) {
    if (*fd != -1) {
      fd_map.emplace(*fd, fd);
    }
  }

  for (auto [old_fd, new_fd] : move_fds) {
    if (*old_fd != -1) {
      fd_map.emplace(*old_fd, old_fd);
    }
  }

  for (auto [old_fd, new_fd] : move_fds) {
    if (*old_fd == -1 || *old_fd == new_fd) {
      continue;
    }

    // Make sure we won't override another fd
    if (auto it = fd_map.find(new_fd); it != fd_map.end()) {
      int fd = dup(new_fd);
      SAPI_RAW_CHECK(fd != -1, "Duplicating an FD failed.");
      *it->second = fd;
      fd_map.emplace(fd, it->second);
      fd_map.erase(it);
    }

    if (dup2(*old_fd, new_fd) == -1) {
      SAPI_RAW_PLOG(FATAL, "Moving temporary to proper FD failed.");
    }

    close(*old_fd);
    fd_map.erase(*old_fd);
    *old_fd = new_fd;
  }
}

struct Pipe {
  FDCloser read;
  FDCloser write;
};

Pipe CreatePipe() {
  int pfds[2];
  SAPI_RAW_PCHECK(pipe(pfds) == 0, "creating pipe");
  return {FDCloser(pfds[0]), FDCloser(pfds[1])};
}

ABSL_ATTRIBUTE_NORETURN void RunInitProcess(pid_t main_pid, FDCloser pipe_fd,
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
  };
  if (pipe_fd.get() >= 0) {
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

  siginfo_t info;
  // Reap children.
  for (;;) {
    int rv = TEMP_FAILURE_RETRY(waitid(P_ALL, -1, &info, WEXITED | __WALL));
    if (rv != 0) {
      _exit(1);
    }

    if (info.si_pid == main_pid) {
      if (pipe_fd.get() >= 0) {
        (void)write(pipe_fd.get(), &info.si_code, sizeof(info.si_code));
        (void)write(pipe_fd.get(), &info.si_status, sizeof(info.si_status));

        rusage usage{};
        getrusage(RUSAGE_CHILDREN, &usage);
        (void)write(pipe_fd.get(), &usage, sizeof(usage));
      }
      _exit(0);
    }
  }
}

absl::Status SendPid(int signaling_fd) {
  // Send our PID (the actual sandboxee process) via SCM_CREDENTIALS.
  // The ancillary message will be attached to the message as SO_PASSCRED is set
  // on the socket.
  char dummy = ' ';
  if (TEMP_FAILURE_RETRY(send(signaling_fd, &dummy, 1, 0)) != 1) {
    return absl::ErrnoToStatus(errno, "Sending PID: send()");
  }
  return absl::OkStatus();
}

absl::StatusOr<pid_t> ReceivePid(int signaling_fd) {
  union {
    struct cmsghdr cmh;
    char ctrl[CMSG_SPACE(sizeof(struct ucred))];
  } ucred_msg{};

  struct msghdr msgh{};
  struct iovec iov{};

  msgh.msg_iov = &iov;
  msgh.msg_iovlen = 1;
  msgh.msg_control = ucred_msg.ctrl;
  msgh.msg_controllen = sizeof(ucred_msg);

  char dummy;
  iov.iov_base = &dummy;
  iov.iov_len = sizeof(char);

  if (TEMP_FAILURE_RETRY(recvmsg(signaling_fd, &msgh, MSG_WAITALL)) != 1) {
    return absl::ErrnoToStatus(errno, "Receiving pid failed: recvmsg");
  }
  struct cmsghdr* cmsgp = CMSG_FIRSTHDR(&msgh);
  if (cmsgp->cmsg_len != CMSG_LEN(sizeof(struct ucred)) ||
      cmsgp->cmsg_level != SOL_SOCKET || cmsgp->cmsg_type != SCM_CREDENTIALS) {
    return absl::InternalError("Receiving pid failed");
  }
  auto* ucredp = reinterpret_cast<struct ucred*>(CMSG_DATA(cmsgp));
  return ucredp->pid;
}

absl::StatusOr<std::string> GetRootMountId(const std::string& proc_id) {
  std::ifstream mounts(absl::StrCat("/proc/", proc_id, "/mountinfo"));
  if (!mounts.good()) {
    return absl::InternalError("Failed to open mountinfo");
  }
  std::string line;
  while (std::getline(mounts, line)) {
    std::vector<absl::string_view> parts =
        absl::StrSplit(line, absl::MaxSplits(' ', 4));
    if (parts.size() >= 4 && parts[3] == "/") {
      return std::string(parts[0]);
    }
  }
  return absl::NotFoundError("Root entry not found in mountinfo");
}

bool IsLikelyChrooted() {
  absl::StatusOr<std::string> self_root_id = GetRootMountId("self");
  if (!self_root_id.ok()) {
    return absl::IsNotFound(self_root_id.status());
  }
  absl::StatusOr<std::string> init_root_id = GetRootMountId("1");
  if (!init_root_id.ok()) {
    return false;
  }
  return *self_root_id != *init_root_id;
}

}  // namespace

void ForkServer::PrepareExecveArgs(const ForkRequest& request,
                                   std::vector<std::string>* args,
                                   std::vector<std::string>* envp) {
  // Prepare arguments for execve.
  for (const auto& arg : request.args()) {
    args->push_back(arg);
  }

  // Prepare environment variables for execve.
  for (const auto& env : request.envs()) {
    envp->push_back(env);
  }

  // The child process should not start any fork-servers.
  envp->push_back(absl::StrCat(kForkServerDisableEnv, "=1"));

  constexpr char kSapiVlogLevel[] = "SAPI_VLOG_LEVEL";
  char* sapi_vlog = getenv(kSapiVlogLevel);
  if (sapi_vlog && strlen(sapi_vlog) > 0) {
    envp->push_back(absl::StrCat(kSapiVlogLevel, "=", sapi_vlog));
  }

  SAPI_RAW_VLOG(1, "Will execute args:['%s'], environment:['%s']",
                absl::StrJoin(*args, "', '").c_str(),
                absl::StrJoin(*envp, "', '").c_str());
}

void ForkServer::LaunchChild(const ForkRequest& request, int execve_fd,
                             uid_t uid, gid_t gid, FDCloser signaling_fd,
                             FDCloser status_fd, bool avoid_pivot_root) const {
  SAPI_RAW_CHECK(request.mode() != FORKSERVER_FORK_UNSPECIFIED,
                 "Forkserver mode is unspecified");

  // Restore the default handler for SIGTERM.
  if (signal(SIGTERM, SIG_DFL) == SIG_ERR) {
    SAPI_RAW_PLOG(WARNING, "signal(SIGTERM, SIG_DFL)");
  }

  const bool will_execve = execve_fd != -1;
  const bool should_sandbox = request.mode() == FORKSERVER_FORK_EXECVE_SANDBOX;

  absl::StatusOr<absl::flat_hash_set<int>> open_fds = sanitizer::GetListOfFDs();
  if (!open_fds.ok()) {
    SAPI_RAW_LOG(WARNING, "Could not get list of current open FDs: %s",
                 std::string(open_fds.status().message()).c_str());
    open_fds = absl::flat_hash_set<int>();
  }
  SanitizeEnvironment();

  InitializeNamespaces(request, uid, gid, avoid_pivot_root);

  auto caps = cap_init();
  SAPI_RAW_CHECK(cap_set_proc(caps) == 0, "while dropping capabilities");
  cap_free(caps);

  // A custom init process is only needed if a new PID NS is created.
  if (request.clone_flags() & CLONE_NEWPID) {
    // Spawn a child process
    pid_t child = util::ForkWithFlags(SIGCHLD);
    if (child < 0) {
      SAPI_RAW_PLOG(FATAL, "Could not spawn init process");
    }
    if (child != 0) {
      if (status_fd.get() >= 0) {
        open_fds->erase(status_fd.get());
      }
      // Close all open fds (equals to CloseAllFDsExcept but does not require
      // /proc to be available).
      for (const auto& fd : *open_fds) {
        if (fd != STDERR_FILENO) {
          close(fd);
        }
      }
      RunInitProcess(child, std::move(status_fd), request.allow_speculation());
    }
    // Send sandboxee pid
    auto status = SendPid(signaling_fd.get());
    SAPI_RAW_CHECK(status.ok(),
                   absl::StrCat("sending pid: ", status.message()).c_str());
  }
  signaling_fd.Close();
  status_fd.Close();

  Client client(comms_);

  // Prepare the arguments before sandboxing (if needed), as doing it after
  // sandoxing can cause syscall violations (e.g. related to memory management).
  std::vector<std::string> args;
  std::vector<std::string> envs;
  if (will_execve) {
    PrepareExecveArgs(request, &args, &envs);
  }

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
    if (comms_->GetConnectionFD() != Comms::kSandbox2ClientCommsFD) {
      envs.push_back(absl::StrCat(Comms::kSandbox2CommsFDEnvVar, "=",
                                  comms_->GetConnectionFD()));
    }
    envs.push_back(client.GetFdMapEnvVar());
  }

  // Convert args and envs before enabling sandbox (it'll allocate which might
  // be blocked).
  util::CharPtrArray argv = util::CharPtrArray::FromStringVector(args);
  util::CharPtrArray envp = util::CharPtrArray::FromStringVector(envs);

  if (should_sandbox) {
    client.EnableSandbox();
  }

  if (will_execve) {
    ExecuteProcess(execve_fd, argv.data(), envp.data());
  }
}

pid_t ForkServer::ServeRequest() {
  ForkRequest fork_request;
  if (!comms_->RecvProtoBuf(&fork_request)) {
    if (comms_->IsTerminated()) {
      return -1;
    }
    SAPI_RAW_LOG(FATAL, "Failed to receive ForkServer request");
  }
  int comms_fd;
  SAPI_RAW_CHECK(comms_->RecvFD(&comms_fd), "Failed to receive Comms FD");

  SAPI_RAW_CHECK(fork_request.mode() != FORKSERVER_FORK_UNSPECIFIED,
                 "Forkserver mode is unspecified");

  int exec_fd = -1;
  if (fork_request.mode() == FORKSERVER_FORK_EXECVE ||
      fork_request.mode() == FORKSERVER_FORK_EXECVE_SANDBOX) {
    SAPI_RAW_CHECK(comms_->RecvFD(&exec_fd), "Failed to receive Exec FD");
  }

  // Make the kernel notify us with SIGCHLD when the process terminates.
  // We use sigaction(SIGCHLD, flags=SA_NOCLDWAIT) in combination with
  // this to make sure the zombie process is reaped immediately.
  int clone_flags = fork_request.clone_flags() | SIGCHLD;

  // Store uid and gid since they will change if CLONE_NEWUSER is set.
  uid_t uid = getuid();
  uid_t gid = getgid();

  Pipe pipe_fds;
  if (fork_request.monitor_type() == FORKSERVER_MONITOR_UNOTIFY) {
    pipe_fds = CreatePipe();
  }

  int socketpair_fds[2];
  SAPI_RAW_PCHECK(
      socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, socketpair_fds) == 0,
      "creating signaling socketpair");
  for (int i = 0; i < 2; ++i) {
    int val = 1;
    SAPI_RAW_PCHECK(setsockopt(socketpair_fds[i], SOL_SOCKET, SO_PASSCRED, &val,
                               sizeof(val)) == 0,
                    "setsockopt failed");
  }

  FDCloser signaling_fds[] = {FDCloser(socketpair_fds[0]),
                              FDCloser(socketpair_fds[1])};

  // Note: init_pid will be overwritten with the actual init pid if the init
  //       process was started or stays at 0 if that is not needed - no pidns.
  pid_t init_pid = 0;
  pid_t sandboxee_pid = -1;
  bool avoid_pivot_root = clone_flags & (CLONE_NEWUSER | CLONE_NEWNS);
  if (avoid_pivot_root) {
    // Create initial namespaces only when they're first needed.
    // This allows sandbox2 to be still used without any namespaces support
    if (initial_mntns_fd_ == -1) {
      CreateInitialNamespaces();
    }
    if (fork_request.netns_mode() == NETNS_MODE_SHARED_PER_FORKSERVER &&
        initial_netns_fd_ == -1) {
      CreateForkserverSharedNetworkNamespace();
    }
    // We first just fork a child, which will join the initial namespaces
    // Note: Not a regular fork() as one really needs to be single-threaded to
    //       setns and this is not the case with TSAN.
    pid_t pid = util::ForkWithFlags(SIGCHLD);
    SAPI_RAW_PCHECK(pid != -1, "fork failed");
    if (pid == 0) {
      SAPI_RAW_PCHECK(setns(initial_userns_fd_, CLONE_NEWUSER) != -1,
                      "joining initial user namespace");
      SAPI_RAW_PCHECK(setns(initial_mntns_fd_, CLONE_NEWNS) != -1,
                      "joining initial mnt namespace");
      if (fork_request.netns_mode() == NETNS_MODE_SHARED_PER_FORKSERVER) {
        SAPI_RAW_PCHECK(setns(initial_netns_fd_, CLONE_NEWNET) != -1,
                        "joining initial net namespace");
        close(initial_netns_fd_);
      }
      close(initial_userns_fd_);
      close(initial_mntns_fd_);
      // Do not create new userns it will be unshared later
      sandboxee_pid =
          util::ForkWithFlags((clone_flags & ~CLONE_NEWUSER) | CLONE_PARENT);
      if (sandboxee_pid == -1) {
        SAPI_RAW_LOG(ERROR, "util::ForkWithFlags(%x)", clone_flags);
      }
      if (sandboxee_pid != 0) {
        _exit(0);
      }
      // Send sandboxee pid
      absl::Status status = SendPid(signaling_fds[1].get());
      SAPI_RAW_CHECK(status.ok(),
                     absl::StrCat("sending pid: ", status.message()).c_str());
    }
  } else {
    sandboxee_pid = util::ForkWithFlags(clone_flags);
    if (sandboxee_pid == -1) {
      SAPI_RAW_LOG(ERROR, "util::ForkWithFlags(%x)", clone_flags);
    }
    if (sandboxee_pid == 0) {
      close(initial_userns_fd_);
      close(initial_mntns_fd_);
    }
  }

  // Child.
  if (sandboxee_pid == 0) {
    signaling_fds[0].Close();
    pipe_fds.read.Close();
    // Make sure we override the forkserver's comms fd
    comms_->Terminate();
    if (exec_fd != -1) {
      int signaling_fd = signaling_fds[1].Release();
      int pipe_fd = pipe_fds.write.Release();
      MoveFDs({{&exec_fd, Comms::kSandbox2TargetExecFD},
               {&comms_fd, Comms::kSandbox2ClientCommsFD}},
              {&signaling_fd, &pipe_fd});
      signaling_fds[1] = FDCloser(signaling_fd);
      pipe_fds.write = FDCloser(pipe_fd);
    }
    *comms_ = Comms(comms_fd);
    LaunchChild(fork_request, exec_fd, uid, gid, std::move(signaling_fds[1]),
                std::move(pipe_fds.write), avoid_pivot_root);
    return sandboxee_pid;
  }

  signaling_fds[1].Close();

  if (avoid_pivot_root) {
    if (auto pid = ReceivePid(signaling_fds[0].get()); !pid.ok()) {
      SAPI_RAW_LOG(ERROR, "%s", std::string(pid.status().message()).c_str());
    } else {
      sandboxee_pid = *pid;
    }
  }

  if (fork_request.clone_flags() & CLONE_NEWPID) {
    // The pid of the init process is equal to the child process that we've
    // previously forked.
    init_pid = sandboxee_pid;
    sandboxee_pid = -1;
    // And the actual sandboxee is forked from the init process, so we need to
    // receive the actual PID.
    if (auto pid = ReceivePid(signaling_fds[0].get()); !pid.ok()) {
      SAPI_RAW_LOG(ERROR, "%s", std::string(pid.status().message()).c_str());
      if (init_pid != -1) {
        kill(init_pid, SIGKILL);
      }
      init_pid = -1;
    } else {
      sandboxee_pid = *pid;
    }
  }

  // Parent.
  pipe_fds.write.Close();
  close(comms_fd);
  if (exec_fd >= 0) {
    close(exec_fd);
  }
  SAPI_RAW_CHECK(comms_->SendInt32(init_pid),
                 absl::StrCat("Failed to send init PID: ", init_pid).c_str());
  SAPI_RAW_CHECK(
      comms_->SendInt32(sandboxee_pid),
      absl::StrCat("Failed to send sandboxee PID: ", sandboxee_pid).c_str());

  if (pipe_fds.read.get() >= 0) {
    SAPI_RAW_CHECK(comms_->SendFD(pipe_fds.read.get()),
                   "Failed to send status pipe");
  }
  return sandboxee_pid;
}

bool ForkServer::IsTerminated() const { return comms_->IsTerminated(); }

bool ForkServer::Initialize() {
  // For safety drop as many capabilities as possible.
  // Note that cap_t is actually a pointer.
  cap_t have_caps = cap_get_proc();  // caps we currently have
  SAPI_RAW_CHECK(have_caps, "failed to cap_get_proc()");
  cap_t wanted_caps = cap_init();  // starts as empty set, ie. no caps
  SAPI_RAW_CHECK(wanted_caps, "failed to cap_init()");

  // CAP_SYS_PTRACE appears to be needed for apparmor (or possibly yama)
  // CAP_SETFCAP is needed on newer kernels (5.10 needs it, 4.15 does not)
  for (cap_value_t cap : {CAP_SYS_PTRACE, CAP_SETFCAP}) {
    for (cap_flag_t flag : {CAP_EFFECTIVE, CAP_PERMITTED}) {
      cap_flag_value_t value;
      int rc = cap_get_flag(have_caps, cap, flag, &value);
      SAPI_RAW_CHECK(!rc, "cap_get_flag");
      if (value == CAP_SET) {
        cap_value_t caps_to_set[1] = {
            cap,
        };
        rc = cap_set_flag(wanted_caps, flag, 1, caps_to_set, CAP_SET);
        SAPI_RAW_CHECK(!rc, "cap_set_flag");
      }
    }
  }

  SAPI_RAW_CHECK(!cap_set_proc(wanted_caps), "while dropping capabilities");
  SAPI_RAW_CHECK(!cap_free(wanted_caps), "while freeing wanted_caps");
  SAPI_RAW_CHECK(!cap_free(have_caps), "while freeing have_caps");

  // All processes spawned by the fork'd/execute'd process will see this process
  // as /sbin/init. Therefore it will receive (and ignore) their final status
  // (see the next comment as well). PR_SET_CHILD_SUBREAPER is available since
  // kernel version 3.4, so don't panic if it fails.
  if (prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) == -1) {
    SAPI_RAW_VLOG(3, "prctl(PR_SET_CHILD_SUBREAPER, 1): %s [%d]",
                  StrError(errno).c_str(), errno);
  }

  // Don't convert terminated child processes into zombies. It's up to the
  // sandbox (Monitor) to track them and receive/report their final status.
  struct sigaction sa;
  sa.sa_handler = SIG_DFL;
  sa.sa_flags = SA_NOCLDWAIT;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGCHLD, &sa, nullptr) == -1) {
    SAPI_RAW_PLOG(ERROR, "sigaction(SIGCHLD, flags=SA_NOCLDWAIT)");
    return false;
  }
  return true;
}

void ForkServer::CreateInitialNamespaces() {
  // Spawn a new process to create initial user and mount namespaces to be used
  // as a base for each namespaced sandboxee.

  // Store uid and gid to create mappings after CLONE_NEWUSER
  uid_t uid = getuid();
  gid_t gid = getgid();

  // Socket to synchronize so that we open ns fds before process dies
  Pipe create_pipe = CreatePipe();
  Pipe open_pipe = CreatePipe();
  pid_t pid = util::ForkWithFlags(CLONE_NEWUSER | CLONE_NEWNS | SIGCHLD);
  if (pid == -1 && errno == EPERM && IsLikelyChrooted()) {
    SAPI_RAW_LOG(FATAL,
                 "failed to fork initial namespaces process: parent process is "
                 "likely chrooted");
  }
  SAPI_RAW_PCHECK(pid != -1, "failed to fork initial namespaces process");
  char value = ' ';
  if (pid == 0) {
    create_pipe.read.Close();
    open_pipe.write.Close();
    Namespace::InitializeInitialNamespaces(uid, gid);
    SAPI_RAW_PCHECK(TEMP_FAILURE_RETRY(write(create_pipe.write.get(), &value,
                                             sizeof(value))) == sizeof(value),
                    "synchronizing initial namespaces creation");
    SAPI_RAW_PCHECK(TEMP_FAILURE_RETRY(read(open_pipe.read.get(), &value,
                                            sizeof(value))) == sizeof(value),
                    "synchronizing initial namespaces creation");
    SAPI_RAW_PCHECK(chroot("/realroot") == 0,
                    "chrooting prior to dumping coverage");
    util::DumpCoverageData();
    _exit(0);
  }
  open_pipe.read.Close();
  create_pipe.write.Close();
  SAPI_RAW_PCHECK(TEMP_FAILURE_RETRY(read(create_pipe.read.get(), &value,
                                          sizeof(value))) == sizeof(value),
                  "synchronizing initial namespaces creation");
  initial_userns_fd_ = open(absl::StrCat("/proc/", pid, "/ns/user").c_str(),
                            O_RDONLY | O_CLOEXEC);
  SAPI_RAW_PCHECK(initial_userns_fd_ != -1, "getting initial userns fd");
  initial_mntns_fd_ = open(absl::StrCat("/proc/", pid, "/ns/mnt").c_str(),
                           O_RDONLY | O_CLOEXEC);
  SAPI_RAW_PCHECK(initial_mntns_fd_ != -1, "getting initial mntns fd");
  SAPI_RAW_PCHECK(TEMP_FAILURE_RETRY(write(open_pipe.write.get(), &value,
                                           sizeof(value))) == sizeof(value),
                  "synchronizing initial namespaces creation");
}

void ForkServer::CreateForkserverSharedNetworkNamespace() {
  Pipe create_pipe = CreatePipe();
  Pipe open_pipe = CreatePipe();
  pid_t pid = util::ForkWithFlags(SIGCHLD);
  SAPI_RAW_PCHECK(pid != -1, "failed to fork shared netns process");
  char value = ' ';
  if (pid == 0) {
    create_pipe.read.Close();
    open_pipe.write.Close();
    SAPI_RAW_PCHECK(setns(initial_userns_fd_, CLONE_NEWUSER) == 0,
                    "joining initial user namespace");
    SAPI_RAW_PCHECK(unshare(CLONE_NEWNET) == 0, "unsharing netns");
    SAPI_RAW_PCHECK(TEMP_FAILURE_RETRY(write(create_pipe.write.get(), &value,
                                             sizeof(value))) == sizeof(value),
                    "synchronizing shared netns creation");
    SAPI_RAW_PCHECK(TEMP_FAILURE_RETRY(read(open_pipe.read.get(), &value,
                                            sizeof(value))) == sizeof(value),
                    "synchronizing shared netns creation");
    util::DumpCoverageData();
    _exit(0);
  }
  open_pipe.read.Close();
  create_pipe.write.Close();
  SAPI_RAW_PCHECK(TEMP_FAILURE_RETRY(read(create_pipe.read.get(), &value,
                                          sizeof(value))) == sizeof(value),
                  "synchronizing shared netns creation");
  initial_netns_fd_ = open(absl::StrCat("/proc/", pid, "/ns/net").c_str(),
                           O_RDONLY | O_CLOEXEC);
  SAPI_RAW_PCHECK(initial_netns_fd_ != -1, "getting initial netns fd");
  SAPI_RAW_PCHECK(TEMP_FAILURE_RETRY(write(open_pipe.write.get(), &value,
                                           sizeof(value))) == sizeof(value),
                  "synchronizing initial namespaces creation");
}

void ForkServer::SanitizeEnvironment() const {
  // Mark all file descriptors, except the standard ones (needed
  // for proper sandboxed process operations), as close-on-exec.
  absl::Status status = sanitizer::SanitizeCurrentProcess(
      {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, comms_->GetConnectionFD()},
      /* close_fds = */ false);
  SAPI_RAW_CHECK(
      status.ok(),
      absl::StrCat("while sanitizing process: ", status.message()).c_str());
}

void ForkServer::ExecuteProcess(int execve_fd, const char* const* argv,
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

void ForkServer::InitializeNamespaces(const ForkRequest& request, uid_t uid,
                                      gid_t gid, bool avoid_pivot_root) {
  if (!request.has_mount_tree()) {
    return;
  }
  Namespace::InitializeNamespaces(
      uid, gid, request.clone_flags(), Mounts(request.mount_tree()),
      request.hostname(), avoid_pivot_root, request.allow_mount_propagation());
}

}  // namespace sandbox2
