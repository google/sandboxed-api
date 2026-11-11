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

#include "sandboxed_api/sandbox2/forkserver.h"

#include <fcntl.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <string>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/fork_client.h"
#include "sandboxed_api/sandbox2/forkingclient.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/sandbox2/global_forkclient.h"
#include "sandboxed_api/sandbox2/ipc.h"
#include "sandboxed_api/testing.h"

namespace sandbox2 {

using ::sapi::GetTestSourcePath;

class IpcPeer {
 public:
  explicit IpcPeer(IPC* ipc) : ipc_(ipc) {}

  void SetUpServerSideComms(int fd) { ipc_->SetUpServerSideComms(fd); }
  Comms* comms() const { return ipc_->comms(); }

 private:
  IPC* ipc_;
};

int GetTestcaseFd() {
  const std::string path = GetTestSourcePath("sandbox2/testcases/sendrecv");
  return open(path.c_str(), O_RDONLY);
}

pid_t TestSingleRequest(Mode mode, int exec_fd,
                        ForkClient* fork_client = nullptr) {
  ForkRequest fork_req;
  IPC ipc;
  int sv[2];
  // Setup IPC
  PCHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != -1);
  IpcPeer ipc_peer(&ipc);
  ipc_peer.SetUpServerSideComms(sv[1]);
  // Setup fork_req
  fork_req.set_mode(mode);
  fork_req.add_args("/binary");
  fork_req.add_envs("FOO=1");

  SandboxeeProcess process =
      GlobalForkClient::SendRequest(fork_req, exec_fd, sv[0], fork_client);
  if (process.main_pid != -1) {
    VLOG(1) << "TestSingleRequest: Waiting for pid=" << process.main_pid;
    waitpid(process.main_pid, nullptr, 0);
  }

  close(sv[0]);
  if (mode == FORKSERVER_FORK_EXECVE && process.main_pid != -1) {
    bool b = false;
    EXPECT_TRUE(ipc_peer.comms()->RecvBool(&b));
    EXPECT_TRUE(b);
    EXPECT_TRUE(ipc_peer.comms()->SendBool(true));
  }
  return process.main_pid;
}

TEST(ForkserverTest, SimpleFork) {
  // Make sure that the regular fork request works.
  ASSERT_NE(TestSingleRequest(FORKSERVER_FORK, -1), -1);
}

TEST(ForkserverTest, SimpleForkNoZombie) {
  // Make sure that we don't create zombies.
  pid_t child = TestSingleRequest(FORKSERVER_FORK, -1);
  ASSERT_NE(child, -1);
  std::string proc = absl::StrCat("/proc/", child, "/cmdline");

  // Give the kernel some time to clean up.
  // Poll every 10ms up to 500 times (5s)
  bool process_reaped = false;
  for (int i = 0; i < 500; i++) {
    if (access(proc.c_str(), F_OK) == -1) {
      process_reaped = true;
      break;
    }
    usleep(10 * 1000);  // 10 ms
  }
  EXPECT_TRUE(process_reaped);
}

TEST(ForkserverTest, ForkExecveWorks) {
  // Run a test binary through the FORK_EXECVE request.
  int exec_fd = GetTestcaseFd();
  PCHECK(exec_fd != -1) << "Could not open test binary";
  ASSERT_NE(TestSingleRequest(FORKSERVER_FORK_EXECVE, exec_fd), -1);
}

TEST(ForkserverTest, ForkExecveTargetFdCollision) {
  int sv[2];
  ASSERT_NE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), -1);
  pid_t fs_pid = fork();
  ASSERT_NE(fs_pid, -1);
  if (fs_pid == 0) {
    close(sv[0]);
    if (sv[1] != Comms::kSandbox2ClientCommsFD) {
      PCHECK(dup2(sv[1], Comms::kSandbox2ClientCommsFD) != -1);
      close(sv[1]);
    }
    // Occupy FDs up to kSandbox2TargetExecFD - 2 so that after setup_comms
    // takes kSandbox2TargetExecFD - 1 (1021) and comms_->Terminate() frees
    // kSandbox2ClientCommsFD (1023) in the child, ReceiveFDs() receives
    // comms_fd_ on 1022 and execve_fd_ on 1023, triggering FD collision
    // resolution in MoveToPredefiedFDs().
    for (int fd = 3; fd < Comms::kSandbox2TargetExecFD - 1; ++fd) {
      if (fcntl(fd, F_GETFD) == -1) {
        PCHECK(dup2(STDIN_FILENO, fd) != -1);
      }
    }
    close(Comms::kSandbox2TargetExecFD - 1);
    close(Comms::kSandbox2TargetExecFD);

    Comms comms(Comms::kSandbox2ClientCommsFD);
    ForkingClient forking_client(&comms);
    forking_client.EnterForkLoop();
    _exit(EXIT_FAILURE);
  }
  close(sv[1]);
  Comms comms(sv[0]);
  ForkClient fork_client(fs_pid, &comms);
  int exec_fd = GetTestcaseFd();
  PCHECK(exec_fd != -1) << "Could not open test binary";
  EXPECT_NE(TestSingleRequest(FORKSERVER_FORK_EXECVE, exec_fd, &fork_client),
            -1);
  close(exec_fd);
  comms.Terminate();
  int status;
  ASSERT_EQ(waitpid(fs_pid, &status, 0), fs_pid);
  ASSERT_TRUE(WIFEXITED(status)) << "status=" << status;
  EXPECT_EQ(WEXITSTATUS(status), EXIT_SUCCESS);
}

TEST(ForkserverTest, ForkExecveSandboxWithoutPolicy) {
  // Run a test binary through the FORKSERVER_FORK_EXECVE_SANDBOX request.
  int exec_fd = GetTestcaseFd();
  PCHECK(exec_fd != -1) << "Could not open test binary";
}

TEST(ForkserverTest, IsLikelyChrootedWorks) {
  EXPECT_FALSE(internal::IsLikelyChrooted());

  pid_t pid = fork();
  ASSERT_NE(pid, -1);
  if (pid == 0) {
    PCHECK(unshare(CLONE_NEWUSER | CLONE_NEWNS) == 0);
    PCHECK(mount("/", "/tmp", "", MS_BIND | MS_REC, nullptr) == 0);
    PCHECK(chroot("/tmp") == 0);
    PCHECK(chdir("/") == 0);
    _exit(internal::IsLikelyChrooted() ? EXIT_SUCCESS : EXIT_FAILURE);
  }
  int status;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "status=" << status;
  EXPECT_EQ(WEXITSTATUS(status), EXIT_SUCCESS);
}

}  // namespace sandbox2
