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
#include <fstream>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "libcap/include/sys/capability.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/forkedprocess.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/sandbox2/latency_stop_watch.h"
#include "sandboxed_api/sandbox2/namespace.h"
#include "sandboxed_api/sandbox2/setup_latency_breakdown.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/strerror.h"

namespace sandbox2 {
namespace {

using ::sapi::StrError;
using ::sapi::file_util::fileops::FDCloser;

struct UnixSocketPair {
  FDCloser sock[2];
};

UnixSocketPair CreateUnixSocketPair(bool passcred = false) {
  int sv[2];
  SAPI_RAW_PCHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) == 0,
                  "creating socketpair");
  UnixSocketPair result = {FDCloser(sv[0]), FDCloser(sv[1])};
  if (passcred) {
    for (int i = 0; i < 2; ++i) {
      int val = 1;
      SAPI_RAW_PCHECK(setsockopt(result.sock[i].get(), SOL_SOCKET, SO_PASSCRED,
                                 &val, sizeof(val)) == 0,
                      "setsockopt failed");
    }
  }
  return result;
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

void ForkServer::HandleInitializeRequest(const ForkRequest& fork_request,
                                         Comms setup_comms) {
  switch (fork_request.initialization_type()) {
    case ForkRequest::INITIALIZE_INITIAL_NAMESPACES:
      CreateInitialNamespaces(std::move(setup_comms));
      break;
    case ForkRequest::INITIALIZE_SHARED_PID_NAMESPACES:
      CreateSharedPidNamespaces(std::move(setup_comms));
      break;
    case ForkRequest::INITIALIZE_EMPTY_NETNS:
      CreateEmptyNetworkNamespace(std::move(setup_comms));
      break;
    case ForkRequest::INITIALIZE_SHARED_MNTNS:
      CreateMountNamespace(std::move(setup_comms));
      break;
    default:
      SAPI_RAW_LOG(FATAL, "Unsupported initialization type: %d",
                   fork_request.initialization_type());
  }
  util::DumpCoverageData();
  _exit(0);
}

pid_t ForkServer::ServeRequest() {
  ForkRequest fork_request;
  if (!comms_->RecvProtoBuf(&fork_request)) {
    if (comms_->IsTerminated()) {
      return -1;
    }
    SAPI_RAW_LOG(FATAL, "Failed to receive ForkServer request");
  }
  SetupLatencyBreakdown latency_breakdown;
  LatencyStopWatch latency_stop_watch;
  SAPI_RAW_CHECK(fork_request.mode() != FORKSERVER_FORK_UNSPECIFIED,
                 "Forkserver mode is unspecified");

  // Create a new comms channel to coordinate the child setup.
  Comms setup_comms = [this] {
    UnixSocketPair setup_socketpair = CreateUnixSocketPair(/*passcred=*/true);
    SAPI_RAW_PCHECK(comms_->SendFD(setup_socketpair.sock[1].get()),
                    "Failed to send setup socket");
    return Comms(setup_socketpair.sock[0].Release());
  }();
  latency_breakdown.SetLatency(SetupLatencyBreakdown::kSetupCommsCreation,
                               latency_stop_watch.LapTime());

  if (fork_request.mode() == FORKSERVER_INITIALIZE) {
    // Note: Not a regular fork() as one really needs to be single-threaded to
    //       setns and this is not the case with TSAN.
    pid_t pid = util::ForkWithFlags(SIGCHLD);
    SAPI_RAW_PCHECK(pid != -1, "fork failed");
    if (pid == 0) {
      HandleInitializeRequest(fork_request, std::move(setup_comms));
    }
    return pid;
  }

  // We fork a child early on to do the rest of the setup.
  const bool has_namespaces = fork_request.clone_flags() & CLONE_NEWUSER;
  pid_t pid;
  if (has_namespaces) {
    // Note: Not a regular fork() as one really needs to be single-threaded to
    //       setns and this is not the case with TSAN.
    pid = util::ForkWithFlags(SIGCHLD);
  } else {
    // Use regular fork() so that pthread's state is properly initialized in
    // the child process.
    pid = fork();
  }
  SAPI_RAW_PCHECK(pid != -1, "fork failed");
  if (pid == 0) {
    latency_breakdown.SetLatency(SetupLatencyBreakdown::kSetupProcessFork,
                                 latency_stop_watch.LapTime());
    // Make sure we don't use the forkserver's comms in the forked process.
    comms_->Terminate();
    ForkedProcess forked(fork_request, std::move(setup_comms),
                         latency_breakdown);
    *comms_ = forked.Setup();
  }
  return pid;
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

void ForkServer::CreateInitialNamespaces(Comms setup_comms) {
  uid_t uid = getuid();
  gid_t gid = getgid();
  int res = unshare(CLONE_NEWUSER | CLONE_NEWNS);
  if (res == -1 && errno == EPERM && IsLikelyChrooted()) {
    SAPI_RAW_LOG(FATAL,
                 "failed to unshare initial namespaces: parent process is "
                 "likely chrooted");
  }
  FDCloser userns_fd(
      open(absl::StrCat("/proc/self/ns/user").c_str(), O_RDONLY | O_CLOEXEC));
  SAPI_RAW_PCHECK(userns_fd.get() != -1, "getting initial userns fd");
  FDCloser mntns_fd(
      open(absl::StrCat("/proc/self/ns/mnt").c_str(), O_RDONLY | O_CLOEXEC));
  SAPI_RAW_PCHECK(mntns_fd.get() != -1, "getting initial mntns fd");
  Namespace::InitializeInitialNamespaces(uid, gid);
  SAPI_RAW_PCHECK(chroot("/realroot") == 0,
                  "chrooting prior to dumping coverage");
  SAPI_RAW_CHECK(setup_comms.SendFD(userns_fd.get()), "sending mntns fd");
  SAPI_RAW_CHECK(setup_comms.SendFD(mntns_fd.get()), "sending mntns fd");
}

void ForkServer::CreateSharedPidNamespaces(Comms setup_comms) {
  FDCloser userns_fd;
  SAPI_RAW_CHECK(setup_comms.RecvFD(&userns_fd), "getting initial userns fd");
  SAPI_RAW_PCHECK(setns(userns_fd.get(), CLONE_NEWUSER) == 0,
                  "joining initial user namespace");
  pid_t pid =
      util::ForkWithFlags(CLONE_NEWNS | CLONE_NEWPID | CLONE_PARENT | SIGCHLD);
  if (pid == -1 && errno == EPERM && IsLikelyChrooted()) {
    SAPI_RAW_LOG(FATAL,
                 "failed to unshare landlock namespaces: parent process is "
                 "likely chrooted");
  }
  SAPI_RAW_PCHECK(pid != -1, "forking landlock init failed");
  if (pid != 0) {
    return;
  }

  Namespace::InitializeSharedPidNamespaces();
  FDCloser mntns_fd(
      open(absl::StrCat("/proc/self/ns/mnt").c_str(), O_RDONLY | O_CLOEXEC));
  SAPI_RAW_PCHECK(mntns_fd.get() != -1, "getting landlock mntns fd");
  FDCloser pidns_fd(
      open(absl::StrCat("/proc/self/ns/pid").c_str(), O_RDONLY | O_CLOEXEC));
  SAPI_RAW_PCHECK(pidns_fd.get() != -1, "getting landlock pidns fd");

  SAPI_RAW_CHECK(setup_comms.SendFD(mntns_fd.get()),
                 "sending landlock mntns fd");
  SAPI_RAW_CHECK(setup_comms.SendFD(pidns_fd.get()),
                 "sending landlock pidns fd");

  SAPI_RAW_PCHECK(prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) == 0,
                  "prctl(PR_SET_PDEATHSIG, SIGKILL)");
  // We stay alive here to keep the PID namespace active and joinable (as
  // PID 1 of the namespace). When the parent terminates, PR_SET_PDEATHSIG
  // delivers SIGKILL to terminate this init process without leaking.
  while (true) {
    pause();
  }
}

void ForkServer::CreateEmptyNetworkNamespace(Comms setup_comms) {
  FDCloser userns_fd;
  SAPI_RAW_CHECK(setup_comms.RecvFD(&userns_fd), "getting initial userns fd");
  SAPI_RAW_PCHECK(setns(userns_fd.get(), CLONE_NEWUSER) == 0,
                  "joining initial user namespace");
  SAPI_RAW_PCHECK(unshare(CLONE_NEWNET) == 0, "unsharing netns");
  FDCloser netns_fd(
      open(absl::StrCat("/proc/self/ns/net").c_str(), O_RDONLY | O_CLOEXEC));
  SAPI_RAW_PCHECK(netns_fd.get() != -1, "getting netns fd");
  SAPI_RAW_CHECK(setup_comms.SendFD(netns_fd.get()), "sending mntns fd");
}

void ForkServer::CreateMountNamespace(Comms setup_comms) {
  FDCloser userns_fd;
  SAPI_RAW_CHECK(setup_comms.RecvFD(&userns_fd), "getting initial userns fd");
  FDCloser pidns_fd;
  SAPI_RAW_CHECK(setup_comms.RecvFD(&pidns_fd), "getting initial pidns fd");
  FDCloser initial_mntns_fd;
  SAPI_RAW_CHECK(setup_comms.RecvFD(&initial_mntns_fd),
                 "getting initial mntns fd");
  FDCloser shared_netns_fd;
  SAPI_RAW_CHECK(setup_comms.RecvFD(&shared_netns_fd),
                 "getting shared netns fd");
  SAPI_RAW_PCHECK(setns(userns_fd.get(), CLONE_NEWUSER) == 0,
                  "joining initial user namespace");
  SAPI_RAW_PCHECK(setns(pidns_fd.get(), CLONE_NEWPID) == 0,
                  "joining initial pid namespace");
  SAPI_RAW_PCHECK(setns(shared_netns_fd.get(), CLONE_NEWNET) == 0,
                  "joining shared netns namespace");
  pid_t pid = util::ForkWithFlags(CLONE_PARENT | SIGCHLD);
  SAPI_RAW_PCHECK(pid != -1, "fork failed");
  if (pid != 0) {
    // We'll continue just in the child.
    _exit(0);
  }
  SAPI_RAW_PCHECK(setns(initial_mntns_fd.get(), CLONE_NEWNS) == 0,
                  "joining initial mount namespace");
  SAPI_RAW_PCHECK(unshare(CLONE_NEWNS) == 0, "unsharing mntns");
  // Open early as we don't have /proc after the mntns setup.
  FDCloser mntns_fd(open("/proc/self/ns/mnt", O_RDONLY | O_CLOEXEC));
  SAPI_RAW_PCHECK(mntns_fd.get() != -1, "getting mntns fd");
  ForkRequest fork_request;
  SAPI_RAW_CHECK(setup_comms.RecvProtoBuf(fork_request.mutable_mount_specs()),
                 "getting mount specs");
  SetupLatencyBreakdown latency_breakdown;
  fork_request.mutable_mount_specs()->set_use_shared_mount_namespace(false);
  fork_request.set_clone_flags(CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWNET);
  fork_request.set_netns_mode(NETNS_MODE_SHARED_PER_FORKSERVER);
  Namespace::InitializeNamespaces(0, 0, fork_request, latency_breakdown,
                                  /*use_hidepid=*/true);
  SAPI_RAW_CHECK(setup_comms.SendFD(mntns_fd.get()), "sending mntns fd");
}

}  // namespace sandbox2
