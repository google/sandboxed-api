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
#include <sys/socket.h>
#include <syscall.h>
#include <unistd.h>

#include <utility>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/sandbox2/global_forkclient.h"
#include "sandboxed_api/sandbox2/ipc.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/raw_logging.h"

namespace sandbox2 {

using ::sapi::GetTestSourcePath;

class IpcPeer {
 public:
  explicit IpcPeer(IPC* ipc) : ipc_(ipc) {}

  void SetUpServerSideComms(int fd) { ipc_->SetUpServerSideComms(fd); }

 private:
  IPC* ipc_;
};

int GetMinimalTestcaseFd() {
  const std::string path = GetTestSourcePath("sandbox2/testcases/minimal");
  return open(path.c_str(), O_RDONLY);
}

pid_t TestSingleRequest(Mode mode, int exec_fd, int userns_fd) {
  ForkRequest fork_req;
  IPC ipc;
  int sv[2];
  // Setup IPC
  PCHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != -1);
  IpcPeer{&ipc}.SetUpServerSideComms(sv[1]);
  // Setup fork_req
  fork_req.set_mode(mode);
  fork_req.add_args("/binary");
  fork_req.add_envs("FOO=1");

  pid_t pid =
      GlobalForkClient::SendRequest(fork_req, exec_fd, sv[0], userns_fd);
  if (pid != -1) {
    VLOG(1) << "TestSingleRequest: Waiting for pid=" << pid;
    waitpid(pid, nullptr, 0);
  }

  close(sv[0]);
  return pid;
}

TEST(ForkserverTest, SimpleFork) {
  // Make sure that the regular fork request works.
  ASSERT_NE(TestSingleRequest(FORKSERVER_FORK, -1, -1), -1);
}

TEST(ForkserverTest, SimpleForkNoZombie) {
  // Make sure that we don't create zombies.
  pid_t child = TestSingleRequest(FORKSERVER_FORK, -1, -1);
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
  int exec_fd = GetMinimalTestcaseFd();
  PCHECK(exec_fd != -1) << "Could not open test binary";
  ASSERT_NE(TestSingleRequest(FORKSERVER_FORK_EXECVE, exec_fd, -1), -1);
}

TEST(ForkserverTest, ForkExecveSandboxWithoutPolicy) {
  // Run a test binary through the FORKSERVER_FORK_EXECVE_SANDBOX request.
  int exec_fd = GetMinimalTestcaseFd();
  PCHECK(exec_fd != -1) << "Could not open test binary";
}

}  // namespace sandbox2
