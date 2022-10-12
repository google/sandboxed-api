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

#include "sandboxed_api/sandbox2/limits.h"

#include <csignal>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/config.h"
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

TEST(LimitsTest, RLimitASMmapUnderLimit) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/limits");
  std::vector<std::string> args = {path, "1"};  // mmap(1 MiB)
  auto executor = std::make_unique<sandbox2::Executor>(path, args);
  executor->limits()->set_rlimit_as(100ULL << 20);  // 100 MiB

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            sandbox2::PolicyBuilder()
                                .DisableNamespaces()
                                // Don't restrict the syscalls at all.
                                .DangerDefaultAllowAll()
                                .TryBuild());
  sandbox2::Sandbox2 s2(std::move(executor), std::move(policy));
  auto result = s2.Run();

  ASSERT_EQ(result.final_status(), sandbox2::Result::OK);
  EXPECT_EQ(result.reason_code(), 0);
}

TEST(LimitsTest, RLimitASMmapAboveLimit) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/limits");
  std::vector<std::string> args = {path, "2"};  // mmap(100 MiB)
  auto executor = std::make_unique<sandbox2::Executor>(path, args);
  executor->limits()->set_rlimit_as(100ULL << 20);  // 100 MiB

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            sandbox2::PolicyBuilder()
                                .DisableNamespaces()
                                // Don't restrict the syscalls at all.
                                .DangerDefaultAllowAll()
                                .TryBuild());
  sandbox2::Sandbox2 s2(std::move(executor), std::move(policy));
  auto result = s2.Run();

  ASSERT_EQ(result.final_status(), sandbox2::Result::OK);
  EXPECT_EQ(result.reason_code(), 0);
}

TEST(LimitsTest, RLimitASAllocaSmallUnderLimit) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/limits");
  std::vector<std::string> args = {path, "3"};  // alloca(1 MiB)
  auto executor = std::make_unique<sandbox2::Executor>(path, args);
  executor->limits()->set_rlimit_as(100ULL << 20);  // 100 MiB

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            sandbox2::PolicyBuilder()
                                .DisableNamespaces()
                                // Don't restrict the syscalls at all.
                                .DangerDefaultAllowAll()
                                .TryBuild());
  sandbox2::Sandbox2 s2(std::move(executor), std::move(policy));
  auto result = s2.Run();

  ASSERT_EQ(result.final_status(), sandbox2::Result::OK);
  EXPECT_EQ(result.reason_code(), 0);
}

TEST(LimitsTest, RLimitASAllocaBigUnderLimit) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/limits");
  std::vector<std::string> args = {path, "4"};  // alloca(8 MiB)
  auto executor = std::make_unique<sandbox2::Executor>(path, args);
  executor->limits()->set_rlimit_as(100ULL << 20);  // 100 MiB

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            sandbox2::PolicyBuilder()
                                .DisableNamespaces()
                                // Don't restrict the syscalls at all.
                                .DangerDefaultAllowAll()
                                .TryBuild());
  sandbox2::Sandbox2 s2(std::move(executor), std::move(policy));
  auto result = s2.Run();

  ASSERT_EQ(result.final_status(), sandbox2::Result::SIGNALED);
  EXPECT_EQ(result.reason_code(), SIGSEGV);
}

TEST(LimitsTest, RLimitASAllocaBigAboveLimit) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/limits");
  std::vector<std::string> args = {path, "5"};  // alloca(100 MiB)
  auto executor = std::make_unique<sandbox2::Executor>(path, args);
  executor->limits()->set_rlimit_as(100ULL << 20);  // 100 MiB

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            sandbox2::PolicyBuilder()
                                .DisableNamespaces()
                                // Don't restrict the syscalls at all.
                                .DangerDefaultAllowAll()
                                .TryBuild());
  sandbox2::Sandbox2 s2(std::move(executor), std::move(policy));
  auto result = s2.Run();

  ASSERT_EQ(result.final_status(), sandbox2::Result::SIGNALED);
  EXPECT_EQ(result.reason_code(), SIGSEGV);
}

}  // namespace
}  // namespace sandbox2
