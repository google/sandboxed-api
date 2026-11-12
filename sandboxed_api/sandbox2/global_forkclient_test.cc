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

#include "sandboxed_api/sandbox2/global_forkclient.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <vector>

#include "gtest/gtest.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/fork_client.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"

namespace sandbox2 {
namespace {

TEST(GlobalForkClientTest, StartStopGlobalForkserver) {
  GlobalForkClient::EnsureStarted();
  EXPECT_TRUE(GlobalForkClient::IsStarted());
  GlobalForkClient::Shutdown();
  EXPECT_FALSE(GlobalForkClient::IsStarted());
  GlobalForkClient::EnsureStarted();
  EXPECT_TRUE(GlobalForkClient::IsStarted());
}

TEST(GlobalForkClientTest, StartWithCommsFdCollision) {
  GlobalForkClient::Shutdown();
  ASSERT_FALSE(GlobalForkClient::IsStarted());

  // Occupy free FDs below kSandbox2ClientCommsFD - 1 (1022) so that
  // StartGlobalForkServer() allocates exec_fd = 1022 and socketpair() allocates
  // sv[0] (comms_fd) = 1023 (Comms::kSandbox2ClientCommsFD).
  std::vector<int> filler_fds;
  for (int fd = 3; fd < Comms::kSandbox2ClientCommsFD; ++fd) {
    if (fcntl(fd, F_GETFD) == -1) {
      int dup_fd = fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, fd);
      ASSERT_EQ(dup_fd, fd);
      filler_fds.push_back(dup_fd);
    }
  }
  if (!filler_fds.empty() &&
      filler_fds.back() == Comms::kSandbox2ClientCommsFD - 1) {
    close(filler_fds.back());
    filler_fds.pop_back();
  }
  close(Comms::kSandbox2ClientCommsFD);

  GlobalForkClient::EnsureStarted();
  for (int fd : filler_fds) {
    close(fd);
  }
  ASSERT_TRUE(GlobalForkClient::IsStarted());

  int sv[2];
  ASSERT_NE(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv), -1);
  ForkRequest fork_req;
  fork_req.set_mode(FORKSERVER_FORK);
  fork_req.add_args("/binary");
  SandboxeeProcess process = GlobalForkClient::SendRequest(fork_req, -1, sv[0]);
  close(sv[0]);
  close(sv[1]);
  ASSERT_NE(process.main_pid, -1);
  waitpid(process.main_pid, nullptr, 0);
  EXPECT_TRUE(GlobalForkClient::IsStarted());
}

}  // namespace
}  //  namespace sandbox2
