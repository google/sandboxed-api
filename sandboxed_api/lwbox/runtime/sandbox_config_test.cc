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

#include "sandboxed_api/lwbox/runtime/sandbox_config.h"

#include <sys/resource.h>

#include <cstddef>
#include <limits>
#include <memory>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "sandboxed_api/embed_toc.h"
#include "sandboxed_api/sandbox.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox_config.h"

namespace sapi::lwbox {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

class FakeSandbox : public ::sapi::SandboxBase {
 public:
  explicit FakeSandbox(absl::Status init_status)
      : ::sapi::SandboxBase("fake_sandbox"), init_status_(init_status) {}

  absl::Status Init() override {
    active_ = init_status_.ok();
    return init_status_;
  }
  bool is_active() const override { return active_; }
  void Terminate(bool /*attempt_graceful_exit*/) override { active_ = false; }
  // `rpcchannel.h` isn't visible to this package; `sandbox.h` declares
  // `RPCChannel`.
  // NOLINTNEXTLINE(misc-include-cleaner)
  ::sapi::RPCChannel* rpc_channel() const override { return nullptr; }
  absl::Status AwaitExitStatus() override { return absl::OkStatus(); }
  absl::Status SetWallTimeLimit(absl::Duration /*limit*/) const override {
    return absl::UnimplementedError("fake");
  }
  absl::StatusOr<int> GetPid() const override { return 42; }

 private:
  absl::Status init_status_;
  bool active_ = false;
};

TEST(MergedSandboxTest, InitializesAndForwardsToSandbox) {
  MergedSandbox sandbox(std::make_unique<FakeSandbox>(absl::OkStatus()));
  EXPECT_EQ(sandbox.name(), "fake_sandbox");
  EXPECT_TRUE(sandbox.is_active());
  EXPECT_THAT(sandbox.GetPid(), IsOkAndHolds(42));
  EXPECT_THAT(sandbox.SetWallTimeLimit(absl::Seconds(1)),
              StatusIs(absl::StatusCode::kUnimplemented));
  sandbox.Terminate();
  EXPECT_FALSE(sandbox.is_active());
}

TEST(MergedSandboxDeathTest, DiesIfSandboxFailsToInitialize) {
  EXPECT_DEATH(
      {
        MergedSandbox sandbox(std::make_unique<FakeSandbox>(
            absl::InternalError("no sandbox for you")));
      },
      "no sandbox for you");
}

TEST(SandboxConfigTest, DefaultPolicyBuilderBuildsValidPolicy) {
  sandbox2::PolicyBuilder builder = DefaultPolicyBuilder();
  absl::StatusOr<std::unique_ptr<sandbox2::Policy>> policy = builder.TryBuild();
  EXPECT_THAT(policy, IsOk());
}

TEST(SandboxConfigTest, CreateMergedSandboxConfigPopulatesExpectedDefaults) {
  sandbox2::PolicyBuilder builder = DefaultPolicyBuilder();
  absl::StatusOr<std::unique_ptr<sandbox2::Policy>> policy = builder.TryBuild();
  ASSERT_THAT(policy, IsOk());

  constexpr ::sapi::EmbedToc kDummyToc = {
      .name = "merged_sandbox_bin",
      .data = "elf",
  };
  ::sapi::SandboxConfig config =
      CreateMergedSandboxConfig(*std::move(policy), &kDummyToc);

  EXPECT_EQ(config.name, "SapiMergedSandbox");
  EXPECT_TRUE(config.environment_variables.has_value());
  EXPECT_TRUE(config.command_line_flags.has_value());
  EXPECT_NE(config.sandbox2.policy, nullptr);
  EXPECT_TRUE(config.sandbox2.fork_client_context.has_value());
  ASSERT_TRUE(config.sandbox2.shared_memory_config.has_value());
  EXPECT_EQ(config.sandbox2.shared_memory_config->size, 128ULL << 20);
  EXPECT_TRUE(config.sandbox2.shared_memory_config->enable_huge_pages);
  EXPECT_TRUE(config.sandbox2.enable_shared_memory_comms);
  EXPECT_TRUE(config.sandbox2.enable_multithreading);
  ASSERT_TRUE(config.sandbox2.limits.has_value());
  EXPECT_EQ(config.sandbox2.limits->rlimit_as().rlim_cur, RLIM64_INFINITY);
  EXPECT_EQ(config.sandbox2.limits->rlimit_cpu().rlim_cur, RLIM64_INFINITY);
  EXPECT_EQ(config.sandbox2.limits->rlimit_fsize().rlim_cur, RLIM64_INFINITY);
  EXPECT_EQ(config.sandbox2.limits->rlimit_nofile().rlim_cur, RLIM64_INFINITY);
  EXPECT_EQ(config.sandbox2.limits->rlimit_core().rlim_cur, RLIM64_INFINITY);
  EXPECT_EQ(config.sandbox2.limits->wall_time_limit(), absl::ZeroDuration());
}

TEST(SandboxConfigTest, CheckedMultiplyAndCheck) {
  MergedSandbox::Check(absl::OkStatus());
  EXPECT_DEATH(MergedSandbox::Check(absl::InternalError("boom")),
               "SAPI merged sandbox failed");

  EXPECT_EQ(MergedSandbox::CheckedMultiply(6, 7), 42u);
  EXPECT_EQ(
      MergedSandbox::CheckedMultiply(0, std::numeric_limits<size_t>::max()),
      0u);
  EXPECT_DEATH(
      MergedSandbox::CheckedMultiply(std::numeric_limits<size_t>::max(), 2),
      "");
}

}  // namespace
}  // namespace sapi::lwbox
