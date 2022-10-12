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

#include "sandboxed_api/sandbox2/ipc.h"

#include <memory>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sandbox2 {
namespace {

using ::sapi::GetTestSourcePath;

constexpr int kPreferredIpcFd = 812;

class IPCTest : public testing::Test,
                public testing::WithParamInterface<int> {};

// This test verifies that mapping fds by name works if the sandbox is enabled
// before execve.
TEST_P(IPCTest, MapFDByNamePreExecve) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const int fd = GetParam();
  const std::string path = GetTestSourcePath("sandbox2/testcases/ipc");
  std::vector<std::string> args = {path, "1", std::to_string(fd)};
  auto executor = std::make_unique<Executor>(path, args);
  Comms comms(executor->ipc()->ReceiveFd(fd, "ipc_test"));

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            PolicyBuilder()
                                .DisableNamespaces()
                                // Don't restrict the syscalls at all.
                                .DangerDefaultAllowAll()
                                .TryBuild());

  Sandbox2 s2(std::move(executor), std::move(policy));
  s2.RunAsync();

  ASSERT_TRUE(comms.SendString("hello"));
  std::string resp;
  ASSERT_TRUE(s2.comms()->RecvString(&resp));
  ASSERT_EQ(resp, "start");
  ASSERT_TRUE(s2.comms()->SendString("started"));
  ASSERT_TRUE(comms.RecvString(&resp));
  ASSERT_EQ(resp, "world");
  ASSERT_TRUE(s2.comms()->RecvString(&resp));
  ASSERT_EQ(resp, "finish");
  ASSERT_TRUE(s2.comms()->SendString("finished"));

  auto result = s2.AwaitResult();

  ASSERT_EQ(result.final_status(), Result::OK);
  ASSERT_EQ(result.reason_code(), 0);
}

// This test verifies that mapping fds by name works if SandboxMeHere() is
// called by the sandboxee.
TEST_P(IPCTest, MapFDByNamePostExecve) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const int fd = GetParam();
  const std::string path = GetTestSourcePath("sandbox2/testcases/ipc");
  std::vector<std::string> args = {path, "2", std::to_string(fd)};
  auto executor = std::make_unique<Executor>(path, args);
  executor->set_enable_sandbox_before_exec(false);
  Comms comms(executor->ipc()->ReceiveFd(fd, "ipc_test"));

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            PolicyBuilder()
                                .DisableNamespaces()
                                // Don't restrict the syscalls at all.
                                .DangerDefaultAllowAll()
                                .TryBuild());

  Sandbox2 s2(std::move(executor), std::move(policy));
  s2.RunAsync();

  ASSERT_TRUE(comms.SendString("hello"));
  std::string resp;
  ASSERT_TRUE(s2.comms()->RecvString(&resp));
  ASSERT_EQ(resp, "start");
  ASSERT_TRUE(s2.comms()->SendString("started"));
  ASSERT_TRUE(comms.RecvString(&resp));
  ASSERT_EQ(resp, "world");
  ASSERT_TRUE(s2.comms()->RecvString(&resp));
  ASSERT_EQ(resp, "finish");
  ASSERT_TRUE(s2.comms()->SendString("finished"));

  auto result = s2.AwaitResult();

  ASSERT_EQ(result.final_status(), Result::OK);
  ASSERT_EQ(result.reason_code(), 0);
}

TEST(IPCTest, NoMappedFDsPreExecve) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path = GetTestSourcePath("sandbox2/testcases/ipc");
  std::vector<std::string> args = {path, "3"};
  auto executor = std::make_unique<Executor>(path, args);

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            PolicyBuilder()
                                .DisableNamespaces()
                                // Don't restrict the syscalls at all.
                                .DangerDefaultAllowAll()
                                .TryBuild());

  Sandbox2 s2(std::move(executor), std::move(policy));
  auto result = s2.Run();

  ASSERT_EQ(result.final_status(), Result::OK);
  ASSERT_EQ(result.reason_code(), 0);
}

INSTANTIATE_TEST_SUITE_P(NormalFds, IPCTest, testing::Values(kPreferredIpcFd));

INSTANTIATE_TEST_SUITE_P(RestrictedFds, IPCTest,
                         testing::Values(Comms::kSandbox2ClientCommsFD,
                                         Comms::kSandbox2TargetExecFD));

}  // namespace
}  // namespace sandbox2
