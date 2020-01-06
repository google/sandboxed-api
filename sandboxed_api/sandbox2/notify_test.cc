// Copyright 2020 Google LLC. All Rights Reserved.
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

#include "sandboxed_api/sandbox2/notify.h"

#include <syscall.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glog/logging.h>
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_join.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/sandbox2/testing.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"

namespace sandbox2 {
namespace {

// Allow typical syscalls and call SECCOMP_RET_TRACE for personality syscall,
// chosen because unlikely to be called by a regular program.
std::unique_ptr<Policy> NotifyTestcasePolicy() {
  return PolicyBuilder()
      .DisableNamespaces()
      .AllowStaticStartup()
      .AllowExit()
      .AllowRead()
      .AllowWrite()
      .AllowSyscall(__NR_close)
      .AddPolicyOnSyscall(__NR_personality, {SANDBOX2_TRACE})
      .BlockSyscallWithErrno(__NR_open, ENOENT)
      .BlockSyscallWithErrno(__NR_access, ENOENT)
      .BlockSyscallWithErrno(__NR_openat, ENOENT)
      .BlockSyscallWithErrno(__NR_prlimit64, EPERM)
      .BuildOrDie();
}

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

// Test EventSyscallTrap on personality syscall and allow it.
TEST(NotifyTest, AllowPersonality) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path = GetTestSourcePath("sandbox2/testcases/personality");
  std::vector<std::string> args = {path};
  auto executor = absl::make_unique<Executor>(path, args);
  auto policy = NotifyTestcasePolicy();
  ASSERT_THAT(policy, testing::Not(testing::IsNull()));
  auto notify = absl::make_unique<PersonalityNotify>(true);

  Sandbox2 s2(std::move(executor), std::move(policy), std::move(notify));
  auto result = s2.Run();

  ASSERT_EQ(result.final_status(), Result::OK);
  ASSERT_EQ(result.reason_code(), 22);
}

// Test EventSyscallTrap on personality syscall and disallow it.
TEST(NotifyTest, DisallowPersonality) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path = GetTestSourcePath("sandbox2/testcases/personality");
  std::vector<std::string> args = {path};
  auto executor = absl::make_unique<Executor>(path, args);
  auto policy = NotifyTestcasePolicy();
  ASSERT_THAT(policy, testing::Not(testing::IsNull()));
  auto notify = absl::make_unique<PersonalityNotify>(false);

  Sandbox2 s2(std::move(executor), std::move(policy), std::move(notify));
  auto result = s2.Run();

  ASSERT_EQ(result.final_status(), Result::VIOLATION);
  ASSERT_EQ(result.reason_code(), __NR_personality);
}

// Test EventStarted by exchanging data after started but before sandboxed.
TEST(NotifyTest, PrintPidAndComms) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path = GetTestSourcePath("sandbox2/testcases/pidcomms");
  std::vector<std::string> args = {path};
  auto executor = absl::make_unique<Executor>(path, args);
  executor->set_enable_sandbox_before_exec(false);
  auto policy = NotifyTestcasePolicy();
  ASSERT_THAT(policy, testing::Not(testing::IsNull()));
  auto notify = absl::make_unique<PidCommsNotify>();

  Sandbox2 s2(std::move(executor), std::move(policy), std::move(notify));
  auto result = s2.Run();

  ASSERT_EQ(result.final_status(), Result::OK);
  ASSERT_EQ(result.reason_code(), 33);
}

}  // namespace
}  // namespace sandbox2
