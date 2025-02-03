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

#include "sandboxed_api/sandbox2/notify.h"

#include <sys/types.h>
#include <syscall.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/allowlists/trace_all_syscalls.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sandbox2 {
namespace {

using ::sapi::CreateDefaultPermissiveTestPolicy;
using ::sapi::GetTestSourcePath;
using ::sapi::IsOk;
using ::testing::Eq;

// If syscall and its arguments don't match the expected ones, return the
// opposite of the requested values (allow/disallow) to indicate an error.
class PersonalityNotify : public Notify {
 public:
  explicit PersonalityNotify(bool allow) : allow_(allow) {}

  bool EventSyscallTrap(const Syscall& syscall) override {
    if (syscall.nr() != __NR_personality) {
      LOG(ERROR) << "kSyscall==" << syscall.nr();
      return (!allow_);
    }
    Syscall::Args expected_args = {0x1, 0x2, 0x3, 0x4, 0x5, 0x6};
    if (syscall.args() != expected_args) {
      LOG(ERROR) << "args=={" << absl::StrJoin(syscall.args(), ", ") << "}";
      return (!allow_);
    }
    return allow_;
  }

 private:
  // The intended return value from EventSyscallTrap in case all registers
  // match.
  bool allow_;
};

// Print the newly created PID, and exchange data over Comms before sandboxing.
class PidCommsNotify : public Notify {
 public:
  bool EventStarted(pid_t pid, Comms* comms) final {
    LOG(INFO) << "The newly created PID: " << pid;
    bool v;
    return comms->RecvBool(&v);
  }
};

class FinishedNotify : public Notify {
 public:
  bool IsFinished() { return finished_; }
  bool EventStarted(pid_t pid, Comms* comms) override {
    EXPECT_FALSE(finished_);
    return true;
  }
  void EventFinished(const Result& result) override { finished_ = true; }

 private:
  bool finished_ = false;
};

class NotifyTest : public ::testing::TestWithParam<bool> {
 public:
  // Allow typical syscalls and call SECCOMP_RET_TRACE for personality syscall,
  // chosen because unlikely to be called by a regular program.
  std::unique_ptr<Policy> NotifyTestcasePolicy(absl::string_view path) {
    sandbox2::PolicyBuilder builder =
        CreateDefaultPermissiveTestPolicy(path).AddPolicyOnSyscall(
            __NR_personality, {SANDBOX2_TRACE});
    if (GetParam()) {
      builder.CollectStacktracesOnSignal(false);
    }
    return builder.BuildOrDie();
  }
  absl::Status SetUpSandbox(Sandbox2* sandbox) {
    return GetParam() ? sandbox->EnableUnotifyMonitor() : absl::OkStatus();
  }
};

// Test EventSyscallTrap on personality syscall and allow it.
TEST_P(NotifyTest, AllowPersonality) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/personality");
  std::vector<std::string> args = {path};
  Sandbox2 s2(std::make_unique<Executor>(path, args),
              NotifyTestcasePolicy(path),
              std::make_unique<PersonalityNotify>(/*allow=*/true));
  ASSERT_THAT(SetUpSandbox(&s2), IsOk());
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(22));
}

// Test EventSyscallTrap on personality syscall and disallow it.
TEST_P(NotifyTest, DisallowPersonality) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/personality");
  std::vector<std::string> args = {path};
  Sandbox2 s2(std::make_unique<Executor>(path, args),
              NotifyTestcasePolicy(path),
              std::make_unique<PersonalityNotify>(/*allow=*/false));
  ASSERT_THAT(SetUpSandbox(&s2), IsOk());
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(__NR_personality));
}

// Test EventStarted by exchanging data after started but before sandboxed.
TEST_P(NotifyTest, PrintPidAndComms) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/pidcomms");
  std::vector<std::string> args = {path};
  auto executor = std::make_unique<Executor>(path, args);
  executor->set_enable_sandbox_before_exec(false);

  Sandbox2 s2(std::move(executor), NotifyTestcasePolicy(path),
              std::make_unique<PidCommsNotify>());
  ASSERT_THAT(SetUpSandbox(&s2), IsOk());
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(33));
}

// Test EventFinished by exchanging data after started but before sandboxed.
TEST_P(NotifyTest, EventFinished) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/minimal");
  std::vector<std::string> args = {path};
  auto executor = std::make_unique<Executor>(path, args);

  auto notify = std::make_unique<FinishedNotify>();
  FinishedNotify* notify_ptr = notify.get();
  Sandbox2 s2(std::move(executor), NotifyTestcasePolicy(path),
              std::move(notify));
  ASSERT_THAT(SetUpSandbox(&s2), IsOk());
  EXPECT_FALSE(notify_ptr->IsFinished());
  auto result = s2.Run();
  EXPECT_TRUE(notify_ptr->IsFinished());

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));
}

// Test EventSyscallTrap on personality syscall through TraceAllSyscalls
TEST_P(NotifyTest, TraceAllAllowPersonality) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/personality");
  std::vector<std::string> args = {path};
  auto policy = CreateDefaultPermissiveTestPolicy(path)
                    .DefaultAction(TraceAllSyscalls())
                    .BuildOrDie();
  Sandbox2 s2(std::make_unique<Executor>(path, args),
              NotifyTestcasePolicy(path),
              std::make_unique<PersonalityNotify>(/*allow=*/true));

  ASSERT_THAT(SetUpSandbox(&s2), IsOk());
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(22));
}

INSTANTIATE_TEST_SUITE_P(Notify, NotifyTest, ::testing::Values(false, true),
                         [](const ::testing::TestParamInfo<bool>& info) {
                           return info.param ? "UnotifyMonitor"
                                             : "PtraceMonitor";
                         });

}  // namespace
}  // namespace sandbox2
