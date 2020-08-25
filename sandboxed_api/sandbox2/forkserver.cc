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

// Implementation of the sandbox2::ForkServer class.

#include "sandboxed_api/sandbox2/forkserver.h"

#include <asm/types.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "libcap/include/sys/capability.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/fork_client.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/sandbox2/namespace.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/sanitizer.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/sandbox2/unwind/unwind.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/sandbox2/util/strerror.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/statusor.h"

namespace {
// "Moves" the old FD to the new FD number.
// The old FD will be closed, the new one is marked as CLOEXEC.
void MoveToFdNumber(int* old_fd, int new_fd) {
  if (dup2(*old_fd, new_fd) == -1) {
    SAPI_RAW_PLOG(FATAL, "Moving temporary to proper FD failed.");
  }
  close(*old_fd);

  // Try to mark that FD as CLOEXEC.
  int flags = fcntl(new_fd, F_GETFD);
  if (flags == -1 || fcntl(new_fd, F_SETFD, flags | O_CLOEXEC) != 0) {
    SAPI_RAW_PLOG(ERROR, "Marking FD as CLOEXEC failed");
  }

  *old_fd = new_fd;
}

void RunInitProcess(std::set<int> open_fds) {
  if (prctl(PR_SET_NAME, "S2-INIT-PROC", 0, 0, 0) != 0) {
    SAPI_RAW_PLOG(WARNING, "prctl(PR_SET_NAME, 'S2-INIT-PROC')");
  }
  // Close all open fds (equals to CloseAllFDsExcept but does not require /proc
  // to be available).
  for (const auto& fd : open_fds) {
    close(fd);
  }

  // Apply seccomp.
  struct sock_filter code[] = {
      LOAD_ARCH,
      JNE32(sandbox2::Syscall::GetHostAuditArch(), DENY),

      LOAD_SYSCALL_NR,
#ifdef __NR_waitpid
      SYSCALL(__NR_waitpid, ALLOW),
#endif
      SYSCALL(__NR_wait4, ALLOW),
      SYSCALL(__NR_exit, ALLOW),
      SYSCALL(__NR_exit_group, ALLOW),
      DENY,
  };

  struct sock_fprog prog {};
  prog.len = ABSL_ARRAYSIZE(code);
  prog.filter = code;

  SAPI_RAW_CHECK(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0,
                 "Denying new privs");
  SAPI_RAW_CHECK(prctl(PR_SET_KEEPCAPS, 0) == 0, "Dropping caps");
  SAPI_RAW_CHECK(
      syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC,
              reinterpret_cast<uintptr_t>(&prog)) == 0,
      "Enabling seccomp filter");

  pid_t pid;
  int status = 0;

  // Reap children.
  while (true) {
    // Wait until we don't have any children anymore.
    // We cannot watch for the child pid as ptrace steals our waitpid
    // notifications. (See man ptrace / man waitpid).
    pid = TEMP_FAILURE_RETRY(waitpid(-1, &status, __WALL));
    if (pid < 0) {
      if (errno == ECHILD) {
        _exit(0);
      }
      _exit(1);
    }
  }
}

absl::Status SendPid(int signaling_fd) {
  // Send our PID (the actual sandboxee process) via SCM_CREDENTIALS.
  // The ancillary message will be attached to the message as SO_PASSCRED is set
  // on the socket.
  char dummy = ' ';
  if (TEMP_FAILURE_RETRY(send(signaling_fd, &dummy, 1, 0)) != 1) {
    return absl::InternalError(
        absl::StrCat("Sending PID: send: ", sandbox2::StrError(errno)));
  }
  return absl::OkStatus();
}

sapi::StatusOr<pid_t> ReceivePid(int signaling_fd) {
  union {
    struct cmsghdr cmh;
    char ctrl[CMSG_SPACE(sizeof(struct ucred))];
  } ucred_msg{};

  struct msghdr msgh {};
  struct iovec iov {};

  msgh.msg_iov = &iov;
  msgh.msg_iovlen = 1;
  msgh.msg_control = ucred_msg.ctrl;
  msgh.msg_controllen = sizeof(ucred_msg);

  char dummy;
  iov.iov_base = &dummy;
  iov.iov_len = sizeof(char);

  if (TEMP_FAILURE_RETRY(recvmsg(signaling_fd, &msgh, MSG_WAITALL)) != 1) {
    return absl::InternalError(absl::StrCat("Receiving pid failed: recvmsg: ",
                                            sandbox2::StrError(errno)));
  }
  struct cmsghdr* cmsgp = CMSG_FIRSTHDR(&msgh);
  if (cmsgp->cmsg_len != CMSG_LEN(sizeof(struct ucred)) ||
      cmsgp->cmsg_level != SOL_SOCKET || cmsgp->cmsg_type != SCM_CREDENTIALS) {
    return absl::InternalError("Receiving pid failed");
  }
  struct ucred* ucredp = reinterpret_cast<struct ucred*>(CMSG_DATA(cmsgp));
  return ucredp->pid;
}
}  // namespace

namespace sandbox2 {

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
                absl::StrJoin(*args, "', '"), absl::StrJoin(*envp, "', '"));
}

void ForkServer::LaunchChild(const ForkRequest& request, int execve_fd,
                             int client_fd, uid_t uid, gid_t gid,
                             int user_ns_fd, int signaling_fd,
                             bool avoid_pivot_root) const {
  bool will_execve = (request.mode() == FORKSERVER_FORK_EXECVE ||
                      request.mode() == FORKSERVER_FORK_EXECVE_SANDBOX);

  if (request.mode() == FORKSERVER_FORK_JOIN_SANDBOX_UNWIND) {
    SAPI_RAW_CHECK(setns(user_ns_fd, CLONE_NEWUSER) == 0,
                   "Could not join user NS");
    close(user_ns_fd);
  }

  // Prepare the arguments before sandboxing (if needed), as doing it after
  // sandoxing can cause syscall violations (e.g. related to memory management).
  std::vector<std::string> args;
  std::vector<std::string> envs;
  const char** argv = nullptr;
  const char** envp = nullptr;
  if (will_execve) {
    PrepareExecveArgs(request, &args, &envs);
  }

  SanitizeEnvironment(client_fd);

  std::set<int> open_fds;
  if (!sanitizer::GetListOfFDs(&open_fds)) {
    SAPI_RAW_LOG(WARNING, "Could not get list of current open FDs");
  }
  InitializeNamespaces(request, uid, gid, avoid_pivot_root);

  auto caps = cap_init();
  for (auto cap : request.capabilities()) {
    SAPI_RAW_CHECK(cap_set_flag(caps, CAP_PERMITTED, 1, &cap, CAP_SET) == 0,
                   "setting capability %d", cap);
    SAPI_RAW_CHECK(cap_set_flag(caps, CAP_EFFECTIVE, 1, &cap, CAP_SET) == 0,
                   "setting capability %d", cap);
    SAPI_RAW_CHECK(cap_set_flag(caps, CAP_INHERITABLE, 1, &cap, CAP_SET) == 0,
                   "setting capability %d", cap);
  }

  SAPI_RAW_CHECK(cap_set_proc(caps) == 0, "while dropping capabilities");
  cap_free(caps);

  // A custom init process is only needed if a new PID NS is created.
  if (request.clone_flags() & CLONE_NEWPID) {
    // Spawn a child process
    pid_t child;
    {
      child = fork();
    }
    if (child < 0) {
      SAPI_RAW_PLOG(FATAL, "Could not spawn init process");
    }
    if (child != 0) {
      RunInitProcess(open_fds);
    }
    // Send sandboxee pid
    auto status = SendPid(signaling_fd);
    SAPI_RAW_CHECK(status.ok(), "sending pid: %s", status.message());
  }

  if (request.mode() == FORKSERVER_FORK_EXECVE_SANDBOX ||
      request.mode() == FORKSERVER_FORK_JOIN_SANDBOX_UNWIND) {
    // Sandboxing can be enabled either here - just before execve, or somewhere
    // inside the executed binary (e.g. after basic structures have been
    // initialized, and resources acquired). In the latter case, it's up to the
    // sandboxed binary to establish proper Comms channel (using
    // Comms::kSandbox2ClientCommsFD) and call sandbox2::Client::SandboxMeHere()

    // Create a Comms object here and not above, as we know we will execve and
    // therefore not call the Comms destructor, which would otherwise close the
    // comms file descriptor, which we do not want for the general case.
    Comms client_comms(Comms::kSandbox2ClientCommsFD);
    Client c(&client_comms);

    // The following client calls are basically SandboxMeHere. We split it so
    // that we can set up the envp after we received the file descriptors but
    // before we enable the syscall filter.
    c.PrepareEnvironment();

    envs.push_back(c.GetFdMapEnvVar());
    // Convert argv and envs to const char **. No need to free it, as the
    // code will either execve() or exit().
    argv = util::VecStringToCharPtrArr(args);
    envp = util::VecStringToCharPtrArr(envs);

    c.EnableSandbox();
    if (request.mode() == FORKSERVER_FORK_JOIN_SANDBOX_UNWIND) {
      exit(RunLibUnwindAndSymbolizer(&client_comms) ? EXIT_SUCCESS
                                                    : EXIT_FAILURE);
    } else {
      ExecuteProcess(execve_fd, argv, envp);
    }
    abort();
  }

  if (will_execve) {
    argv = util::VecStringToCharPtrArr(args);
    envp = util::VecStringToCharPtrArr(envs);
    ExecuteProcess(execve_fd, argv, envp);
    abort();
  }
}

pid_t ForkServer::ServeRequest() {
  // Keep the low FD numbers clean so that client FD mappings don't interfer
  // with us.
  constexpr int kTargetExecFd = 1022;

  ForkRequest fork_request;
  if (!comms_->RecvProtoBuf(&fork_request)) {
    if (comms_->IsTerminated()) {
      SAPI_RAW_VLOG(1, "ForkServer Comms closed. Exiting");
      exit(0);
    } else {
      SAPI_RAW_LOG(FATAL, "Failed to receive ForkServer request");
    }
  }
  int comms_fd;
  SAPI_RAW_CHECK(comms_->RecvFD(&comms_fd), "Failed to receive Comms FD");

  int exec_fd = -1;
  if (fork_request.mode() == FORKSERVER_FORK_EXECVE ||
      fork_request.mode() == FORKSERVER_FORK_EXECVE_SANDBOX) {
    SAPI_RAW_CHECK(comms_->RecvFD(&exec_fd), "Failed to receive Exec FD");
    // We're duping to a high number here to avoid colliding with the IPC FDs.
    MoveToFdNumber(&exec_fd, kTargetExecFd);
  }

  // Make the kernel notify us with SIGCHLD when the process terminates.
  // We use sigaction(SIGCHLD, flags=SA_NOCLDWAIT) in combination with
  // this to make sure the zombie process is reaped immediately.
  int clone_flags = fork_request.clone_flags() | SIGCHLD;

  int user_ns_fd = -1;
  if (fork_request.mode() == FORKSERVER_FORK_JOIN_SANDBOX_UNWIND) {
    SAPI_RAW_CHECK(comms_->RecvFD(&user_ns_fd),
                   "Failed to receive user namespace fd");
  }

  // Store uid and gid since they will change if CLONE_NEWUSER is set.
  uid_t uid = getuid();
  uid_t gid = getgid();

  int socketpair_fds[2];
  SAPI_RAW_PCHECK(
      socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, socketpair_fds) == 0,
      "creating signaling socketpair");
  for (int i = 0; i < 2; i++) {
    int val = 1;
    SAPI_RAW_PCHECK(setsockopt(socketpair_fds[i], SOL_SOCKET, SO_PASSCRED, &val,
                               sizeof(val)) == 0,
                    "setsockopt failed");
  }

  file_util::fileops::FDCloser fd_closer0{socketpair_fds[0]};
  file_util::fileops::FDCloser fd_closer1{socketpair_fds[1]};

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
      absl::Status status = SendPid(fd_closer1.get());
      SAPI_RAW_CHECK(status.ok(), "sending pid: %s", status.message());
    } else if (auto pid_or = ReceivePid(fd_closer0.get()); !pid_or.ok()) {
      SAPI_RAW_LOG(ERROR, "receiving pid: %s", pid_or.status().message());
    } else {
      sandboxee_pid = pid_or.value();
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
    LaunchChild(fork_request, exec_fd, comms_fd, uid, gid, user_ns_fd,
                fd_closer1.get(), avoid_pivot_root);
    return sandboxee_pid;
  }

  fd_closer1.Close();

  if (fork_request.clone_flags() & CLONE_NEWPID) {
    // The pid of the init process is equal to the child process that we've
    // previously forked.
    init_pid = sandboxee_pid;
    sandboxee_pid = -1;
    // And the actual sandboxee is forked from the init process, so we need to
    // receive the actual PID.
    if (auto pid_or = ReceivePid(fd_closer0.get()); !pid_or.ok()) {
      SAPI_RAW_LOG(ERROR, "%s", pid_or.status().message());
      kill(init_pid, SIGKILL);
      init_pid = -1;
    } else {
      sandboxee_pid = pid_or.value();
    }
  }

  // Parent.
  close(comms_fd);
  if (exec_fd >= 0) {
    close(exec_fd);
  }
  if (user_ns_fd >= 0) {
    close(user_ns_fd);
  }
  SAPI_RAW_CHECK(comms_->SendInt32(init_pid), "Failed to send init PID: %d",
                 init_pid);
  SAPI_RAW_CHECK(comms_->SendInt32(sandboxee_pid),
                 "Failed to send sandboxee PID: %d", sandboxee_pid);
  return sandboxee_pid;
}

bool ForkServer::Initialize() {
  // If the parent goes down, so should we.
  if (prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) != 0) {
    SAPI_RAW_PLOG(ERROR, "prctl(PR_SET_PDEATHSIG, SIGKILL)");
    return false;
  }

  // All processes spawned by the fork'd/execute'd process will see this process
  // as /sbin/init. Therefore it will receive (and ignore) their final status
  // (see the next comment as well). PR_SET_CHILD_SUBREAPER is available since
  // kernel version 3.4, so don't panic if it fails.
  if (prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) == -1) {
    SAPI_RAW_VLOG(3, "prctl(PR_SET_CHILD_SUBREAPER, 1): %s [%d]",
                  StrError(errno), errno);
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
  int fds[2];
  SAPI_RAW_PCHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != -1,
                  "creating socket");
  pid_t pid = util::ForkWithFlags(CLONE_NEWUSER | CLONE_NEWNS | SIGCHLD);
  SAPI_RAW_PCHECK(pid != -1, "failed to fork initial namespaces process");
  char unused = '\0';
  if (pid == 0) {
    close(fds[1]);
    Namespace::InitializeInitialNamespaces(uid, gid);
    SAPI_RAW_PCHECK(TEMP_FAILURE_RETRY(write(fds[0], &unused, 1)) == 1,
                    "synchronizing initial namespaces creation");
    SAPI_RAW_PCHECK(TEMP_FAILURE_RETRY(read(fds[0], &unused, 1)) == 1,
                    "synchronizing initial namespaces creation");
    _exit(0);
  }
  close(fds[0]);
  initial_userns_fd_ = open(absl::StrCat("/proc/", pid, "/ns/user").c_str(),
                            O_RDONLY | O_CLOEXEC);
  SAPI_RAW_PCHECK(initial_userns_fd_ != -1, "getting initial userns fd");
  initial_mntns_fd_ = open(absl::StrCat("/proc/", pid, "/ns/mnt").c_str(),
                           O_RDONLY | O_CLOEXEC);
  SAPI_RAW_PCHECK(initial_mntns_fd_ != -1, "getting initial mntns fd");
  SAPI_RAW_PCHECK(TEMP_FAILURE_RETRY(read(fds[1], &unused, 1)) == 1,
                  "synchronizing initial namespaces creation");
  SAPI_RAW_PCHECK(TEMP_FAILURE_RETRY(write(fds[1], &unused, 1)) == 1,
                  "synchronizing initial namespaces creation");
  close(fds[1]);
}

void ForkServer::SanitizeEnvironment(int client_fd) {
  // Duplicate client's CommsFD onto fd=Comms::kSandbox2ClientCommsFD (1023).
  SAPI_RAW_CHECK(dup2(client_fd, Comms::kSandbox2ClientCommsFD) != -1,
                 "while remapping client comms fd");
  close(client_fd);
  // Mark all file descriptors, except the standard ones (needed
  // for proper sandboxed process operations), as close-on-exec.
  SAPI_RAW_CHECK(sanitizer::SanitizeCurrentProcess(
                     {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO,
                      Comms::kSandbox2ClientCommsFD},
                     /* close_fds = */ false),
                 "while sanitizing process");
}

void ForkServer::ExecuteProcess(int execve_fd, const char** argv,
                                const char** envp) {
  // Do not add any code before execve(), as it's subject to seccomp policies.
  // Indicate that it's a special execve(), by setting 4th, 5th and 6th syscall
  // argument to magic values.
  util::Syscall(
      __NR_execveat, static_cast<uintptr_t>(execve_fd),
      reinterpret_cast<uintptr_t>(""), reinterpret_cast<uintptr_t>(argv),
      reinterpret_cast<uintptr_t>(envp), static_cast<uintptr_t>(AT_EMPTY_PATH),
      reinterpret_cast<uintptr_t>(internal::kExecveMagic));

  int saved_errno = errno;
  SAPI_RAW_PLOG(ERROR, "sandbox2::ForkServer: execveat failed");

  if (saved_errno == ENOSYS) {
    SAPI_RAW_LOG(ERROR,
                 "sandbox2::ForkServer: This is likely caused by running"
                 " sandbox2 on too old a kernel."
    );
  } else if (saved_errno == ENOENT && execve_fd >= 0) {
    // Since we know the file exists, it must be that the file is dynamically
    // linked and the ELF interpreter is what's actually missing.
    SAPI_RAW_LOG(ERROR,
                 "sandbox2::ForkServer: This is likely caused by running"
                 " dynamically-linked sandboxee without calling"
                 " .AddLibrariesForBinary() on the policy builder.");
  }

  util::Syscall(__NR_exit_group, EXIT_FAILURE);
}

void ForkServer::InitializeNamespaces(const ForkRequest& request, uid_t uid,
                                      gid_t gid, bool avoid_pivot_root) {
  if (!request.has_mount_tree()) {
    return;
  }
  int32_t clone_flags = request.clone_flags();
  if (request.mode() == FORKSERVER_FORK_JOIN_SANDBOX_UNWIND) {
    clone_flags = CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC;
    SAPI_RAW_PCHECK(!unshare(clone_flags),
                    "Could not create new namespaces for libunwind");
  }
  Namespace::InitializeNamespaces(
      uid, gid, clone_flags, Mounts(request.mount_tree()),
      request.mode() != FORKSERVER_FORK_JOIN_SANDBOX_UNWIND, request.hostname(),
      avoid_pivot_root);
}

}  // namespace sandbox2
