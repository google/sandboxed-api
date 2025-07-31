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

#include "sandboxed_api/sandbox2/sandbox2.h"

#include <fcntl.h>
#include <syscall.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/fork_client.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/thread.h"

namespace sandbox2 {
namespace {

using ::absl_testing::IsOk;
using ::sapi::CreateDefaultPermissiveTestPolicy;
using ::sapi::GetTestSourcePath;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Lt;
using ::testing::Ne;

class Sandbox2Test : public ::testing::TestWithParam<bool> {
 public:
  PolicyBuilder CreateDefaultTestPolicy(absl::string_view path) {
    PolicyBuilder builder = CreateDefaultPermissiveTestPolicy(path);
    if (GetParam()) {
      builder.CollectStacktracesOnSignal(false);
    }
    return builder;
  }
  absl::Status SetUpSandbox(Sandbox2* sandbox) {
    return GetParam() ? sandbox->EnableUnotifyMonitor() : absl::OkStatus();
  }
};

// Test that aborting inside a sandbox with all userspace core dumping
// disabled reports the signal.
TEST_P(Sandbox2Test, AbortWithoutCoreDumpReturnsSignaled) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/abort");
  std::vector<std::string> args = {
      path,
  };
  auto executor = std::make_unique<Executor>(path, args);

  SAPI_ASSERT_OK_AND_ASSIGN(
      auto policy,
      CreateDefaultTestPolicy(path)
          .TryBuild());
  Sandbox2 sandbox(std::move(executor), std::move(policy));
  ASSERT_THAT(SetUpSandbox(&sandbox), IsOk());
  auto result = sandbox.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::SIGNALED));
  EXPECT_THAT(result.reason_code(), Eq(SIGABRT));
}

// Test that with TSYNC we are able to sandbox when multithreaded.
TEST_P(Sandbox2Test, TsyncNoMemoryChecks) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/tsync");

  auto executor =
      std::make_unique<Executor>(path, std::vector<std::string>{path});
  executor->set_enable_sandbox_before_exec(false);

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            CreateDefaultTestPolicy(path).TryBuild());
  Sandbox2 sandbox(std::move(executor), std::move(policy));
  ASSERT_THAT(SetUpSandbox(&sandbox), IsOk());
  auto result = sandbox.Run();

  // With TSYNC, SandboxMeHere should be able to sandbox when multithreaded.
  ASSERT_EQ(result.final_status(), Result::OK);
  ASSERT_EQ(result.reason_code(), 0);
}

// Tests whether Executor(fd, std::vector<std::string>{path}, envp) constructor
// works as expected.
TEST(ExecutorTest, ExecutorFdConstructor) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/minimal");
  int fd = open(path.c_str(), O_RDONLY);
  ASSERT_NE(fd, -1);

  std::vector<std::string> args = {absl::StrCat("FD:", fd)};
  auto executor = std::make_unique<Executor>(fd, args);

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            CreateDefaultPermissiveTestPolicy(path).TryBuild());
  Sandbox2 sandbox(std::move(executor), std::move(policy));
  auto result = sandbox.Run();

  EXPECT_THAT(sandbox.IsTerminated(), IsTrue());
  ASSERT_EQ(result.final_status(), Result::OK);
}

// Test that rusage is returned correctly.
TEST_P(Sandbox2Test, GetRUsageSandboxee) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/minimal");
  auto executor =
      std::make_unique<Executor>(path, std::vector<std::string>{path});
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            CreateDefaultTestPolicy(path).TryBuild());

  Sandbox2 sandbox(std::move(executor), std::move(policy));
  ASSERT_THAT(SetUpSandbox(&sandbox), IsOk());
  auto result = sandbox.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  ASSERT_TRUE(result.GetRUsageSandboxee().has_value());
  EXPECT_THAT(result.GetRUsageSandboxee()->ru_maxrss, Gt(0));
}

// Tests that we return the correct state when the sandboxee was killed by an
// external signal. Also make sure that we do not have the stack trace.
TEST_P(Sandbox2Test, SandboxeeExternalKill) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/sleep");

  std::vector<std::string> args = {path};
  auto executor = std::make_unique<Executor>(path, args);

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            CreateDefaultTestPolicy(path).TryBuild());
  Sandbox2 sandbox(std::move(executor), std::move(policy));
  ASSERT_THAT(SetUpSandbox(&sandbox), IsOk());
  ASSERT_TRUE(sandbox.RunAsync());
  EXPECT_THAT(sandbox.IsTerminated(), IsFalse());
  absl::SleepFor(absl::Seconds(1));
  sandbox.Kill();
  auto result = sandbox.AwaitResult();
  EXPECT_THAT(sandbox.IsTerminated(), IsTrue());
  EXPECT_EQ(result.final_status(), Result::EXTERNAL_KILL);
  EXPECT_THAT(result.stack_trace(), IsEmpty());
}

TEST_P(Sandbox2Test, SandboxeeKillDontAwait) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/sleep");

  std::vector<std::string> args = {path};
  auto executor = std::make_unique<Executor>(path, args);

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            CreateDefaultTestPolicy(path).TryBuild());
  absl::Time kill_time;
  {
    Sandbox2 sandbox(std::move(executor), std::move(policy));
    ASSERT_THAT(SetUpSandbox(&sandbox), IsOk());
    ASSERT_TRUE(sandbox.RunAsync());
    EXPECT_THAT(sandbox.IsTerminated(), IsFalse());
    absl::SleepFor(absl::Seconds(1));
    sandbox.Kill();
    kill_time = absl::Now();
  }
  absl::Duration elapsed = absl::Now() - kill_time;
  EXPECT_THAT(elapsed, Lt(absl::Milliseconds(200)));
}

// Tests that we do not collect stack traces if it was disabled (signaled).
TEST_P(Sandbox2Test, SandboxeeTimeoutDisabledStacktraces) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/sleep");

  std::vector<std::string> args = {path};
  auto executor = std::make_unique<Executor>(path, args);

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, CreateDefaultTestPolicy(path)
                                             .CollectStacktracesOnTimeout(false)
                                             .TryBuild());
  Sandbox2 sandbox(std::move(executor), std::move(policy));
  ASSERT_THAT(SetUpSandbox(&sandbox), IsOk());
  absl::Time start_time = absl::Now();
  ASSERT_TRUE(sandbox.RunAsync());
  sandbox.set_walltime_limit(absl::Seconds(1));
  auto result = sandbox.AwaitResult();
  EXPECT_EQ(result.final_status(), Result::TIMEOUT);
  auto elapsed = absl::Now() - start_time;
  EXPECT_THAT(elapsed, Lt(absl::Seconds(2)));
  EXPECT_THAT(result.stack_trace(), IsEmpty());
}

// Tests that we do not collect stack traces if it was disabled (violation).
TEST(Sandbox2Test, SandboxeeViolationDisabledStacktraces) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/sleep");

  std::vector<std::string> args = {path};
  auto executor = std::make_unique<Executor>(path, args);

  SAPI_ASSERT_OK_AND_ASSIGN(
      auto policy, PolicyBuilder()
                       // Don't allow anything - Make sure that we'll crash.
                       .CollectStacktracesOnViolation(false)
                       .TryBuild());
  Sandbox2 sandbox(std::move(executor), std::move(policy));
  ASSERT_TRUE(sandbox.RunAsync());
  auto result = sandbox.AwaitResult();
  EXPECT_EQ(result.final_status(), Result::VIOLATION);
  EXPECT_THAT(result.stack_trace(), IsEmpty());
}

TEST_P(Sandbox2Test, SandboxeeNotKilledWhenStartingThreadFinishes) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/minimal");
  std::vector<std::string> args = {path};
  auto executor = std::make_unique<Executor>(path, args);

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            CreateDefaultTestPolicy(path).TryBuild());
  Sandbox2 sandbox(std::move(executor), std::move(policy));
  ASSERT_THAT(SetUpSandbox(&sandbox), IsOk());
  sapi::Thread sandbox_start_thread([&sandbox]() { sandbox.RunAsync(); });
  sandbox_start_thread.Join();
  Result result = sandbox.AwaitResult();
  EXPECT_EQ(result.final_status(), Result::OK);
}

TEST_P(Sandbox2Test, CustomForkserverWorks) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/custom_fork");
  std::vector<std::string> args = {path};
  auto fork_executor = std::make_unique<Executor>(path, args);
  std::unique_ptr<ForkClient> fork_client = fork_executor->StartForkServer();
  ASSERT_THAT(fork_client.get(), Ne(nullptr));

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            CreateDefaultTestPolicy(path).TryBuild());

  Sandbox2 sandbox(std::make_unique<Executor>(fork_client.get()),
                   std::move(policy));
  ASSERT_THAT(SetUpSandbox(&sandbox), IsOk());
  Result result = sandbox.Run();
  EXPECT_EQ(result.final_status(), Result::OK);
}

TEST(StarvationTest, MonitorIsNotStarvedByTheSandboxee) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/starve");

  std::vector<std::string> args = {path};
  auto executor = std::make_unique<Executor>(path, args);
  executor->limits()->set_walltime_limit(absl::Seconds(5));

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            CreateDefaultPermissiveTestPolicy(path).TryBuild());
  Sandbox2 sandbox(std::move(executor), std::move(policy));

  auto start = absl::Now();
  ASSERT_THAT(sandbox.RunAsync(), IsTrue());
  auto result = sandbox.AwaitResult();
  EXPECT_THAT(result.final_status(), Eq(Result::TIMEOUT));

  auto elapsed = absl::Now() - start;
  EXPECT_THAT(elapsed, Lt(absl::Seconds(10)));
}

TEST_P(Sandbox2Test, TerminatingProcessGroup) {
  // Scenario:
  //   Sandboxer process is moved to a new process group after the sandboxee is
  //   launched. Afterwards the process group of sandboxer's parent is killed.
  // Expected result:
  //   The sandboxee should not be killed, sandboxee's status should be properly
  //   reported to the sandboxer.
  int sv[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  util::CharPtrArray argv = util::CharPtrArray::FromStringVector({
      GetTestSourcePath("sandbox2/testcases/terminate_process_group"),
      absl::StrCat("--comms_fd=", sv[1]),
      absl::StrCat("--unotify_monitor=", GetParam()),
  });
  Comms comms(sv[0]);
  pid_t pid = fork();
  if (pid == 0) {
    close(sv[0]);
    util::Execveat(AT_FDCWD, argv.data()[0], argv.data(), environ, 0);
    PLOG(FATAL) << "Could not exeveat";
  }
  close(sv[1]);
  bool unused;
  // Wait for the sandboxee to be started
  ASSERT_TRUE(comms.RecvBool(&unused));
  // Kill sandboxer's parent process group.
  ASSERT_EQ(kill(-pid, SIGTERM), 0);
  // Wait for sandboxer's parent termination.
  int status;
  ASSERT_THAT(waitpid(pid, &status, 0), Eq(pid))
      << absl::ErrnoToStatus(errno, "waiting for process to be terimnated");
  ASSERT_TRUE(WIFSIGNALED(status));
  EXPECT_THAT(WTERMSIG(status), Eq(SIGTERM));
  // Wait for sandboxee to be potentially killed as a result of the parent
  // termination.
  absl::SleepFor(absl::Seconds(1));
  // Communicate to the sandboxee it can exit.
  ASSERT_TRUE(comms.SendBool(true));
  // Wait for notification about clean sandboxee exit.
  ASSERT_TRUE(comms.RecvBool(&unused));
}

INSTANTIATE_TEST_SUITE_P(Sandbox2, Sandbox2Test, ::testing::Values(false, true),
                         [](const ::testing::TestParamInfo<bool>& info) {
                           return info.param ? "UnotifyMonitor"
                                             : "PtraceMonitor";
                         });

}  // namespace
}  // namespace sandbox2
