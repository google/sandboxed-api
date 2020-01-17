// Copyright 2019 Google LLC
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

#include "sandboxed_api/sandbox2/namespace.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <syscall.h>
#include <unistd.h>

#include <utility>

#include <glog/logging.h>
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2/testing.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sandbox2 {
namespace {

TEST(NamespaceTest, FileNamespaceWorks) {
  // Mount /binary_path RO and check that it actually is RO.
  // /etc/passwd should not exist.
  const std::string path = GetTestSourcePath("sandbox2/testcases/namespace");
  std::vector<std::string> args = {path, "0", "/binary_path", "/etc/passwd"};
  auto executor = absl::make_unique<Executor>(path, args);
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, PolicyBuilder()
                                        // Don't restrict the syscalls at all
                                        .DangerDefaultAllowAll()
                                        .AddFileAt(path, "/binary_path")
                                        .TryBuild());

  Sandbox2 sandbox(std::move(executor), std::move(policy));
  auto result = sandbox.Run();

  ASSERT_EQ(result.final_status(), Result::OK);
  EXPECT_EQ(result.reason_code(), 2);
}

TEST(NamespaceTest, UserNamespaceWorks) {
  // Check that getpid() returns 2 (which is the case inside pid NS).
  const std::string path = GetTestSourcePath("sandbox2/testcases/namespace");
  std::vector<std::string> args = {path, "2"};
  {
    auto executor = absl::make_unique<Executor>(path, args);
    SAPI_ASSERT_OK_AND_ASSIGN(auto policy, PolicyBuilder()
                                          // Don't restrict the syscalls at all
                                          .DangerDefaultAllowAll()
                                          .TryBuild());

    Sandbox2 sandbox(std::move(executor), std::move(policy));
    auto result = sandbox.Run();

    ASSERT_EQ(result.final_status(), Result::OK);
    EXPECT_EQ(result.reason_code(), 0);
  }

  // Validate that getpid() does not return 2 when outside of an pid NS.
  {
    auto executor = absl::make_unique<Executor>(path, args);
    SAPI_ASSERT_OK_AND_ASSIGN(auto policy, PolicyBuilder()
                                          .DisableNamespaces()
                                          // Don't restrict the syscalls at all
                                          .DangerDefaultAllowAll()
                                          .TryBuild());

    Sandbox2 sandbox(std::move(executor), std::move(policy));
    auto result = sandbox.Run();

    ASSERT_EQ(result.final_status(), Result::OK);
    EXPECT_NE(result.reason_code(), 0);
  }
}

TEST(NamespaceTest, UserNamespaceIDMapWritten) {
  // Check that the idmap is initialized before the sandbox application is
  // started.
  const std::string path = GetTestSourcePath("sandbox2/testcases/namespace");
  {
    std::vector<std::string> args = {path, "3", "1000", "1000"};
    auto executor = absl::make_unique<Executor>(path, args);
    SAPI_ASSERT_OK_AND_ASSIGN(auto policy, PolicyBuilder()
                                          // Don't restrict the syscalls at all
                                          .DangerDefaultAllowAll()
                                          .TryBuild());

    Sandbox2 sandbox(std::move(executor), std::move(policy));
    auto result = sandbox.Run();

    ASSERT_EQ(result.final_status(), Result::OK);
    EXPECT_EQ(result.reason_code(), 0);
  }

  // Check that the uid/gid is the same when not using namespaces.
  {
    const std::string uid = absl::StrCat(getuid());
    const std::string gid = absl::StrCat(getgid());
    std::vector<std::string> args = {path, "3", uid, gid};
    auto executor = absl::make_unique<Executor>(path, args);
    SAPI_ASSERT_OK_AND_ASSIGN(auto policy, PolicyBuilder()
                                          .DisableNamespaces()
                                          // Don't restrict the syscalls at all
                                          .DangerDefaultAllowAll()
                                          .TryBuild());

    Sandbox2 sandbox(std::move(executor), std::move(policy));
    auto result = sandbox.Run();

    ASSERT_EQ(result.final_status(), Result::OK);
    EXPECT_EQ(result.reason_code(), 0);
  }
}

TEST(NamespaceTest, RootReadOnly) {
  // Mount rw tmpfs at /tmp and check it is rw.
  // Check also that / is ro.
  const std::string path = GetTestSourcePath("sandbox2/testcases/namespace");
  std::vector<std::string> args = {path, "4", "/tmp/testfile", "/testfile"};
  auto executor = absl::make_unique<Executor>(path, args);
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, PolicyBuilder()
                                        // Don't restrict the syscalls at all
                                        .DangerDefaultAllowAll()
                                        .AddTmpfs("/tmp")
                                        .TryBuild());

  Sandbox2 sandbox(std::move(executor), std::move(policy));
  auto result = sandbox.Run();

  ASSERT_EQ(result.final_status(), Result::OK);
  EXPECT_EQ(result.reason_code(), 2);
}

TEST(NamespaceTest, RootWritable) {
  // Mount root rw and check it
  const std::string path = GetTestSourcePath("sandbox2/testcases/namespace");
  std::vector<std::string> args = {path, "4", "/testfile"};
  auto executor = absl::make_unique<Executor>(path, args);
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, PolicyBuilder()
                                        // Don't restrict the syscalls at all
                                        .DangerDefaultAllowAll()
                                        .SetRootWritable()
                                        .TryBuild());

  Sandbox2 sandbox(std::move(executor), std::move(policy));
  auto result = sandbox.Run();

  ASSERT_EQ(result.final_status(), Result::OK);
  EXPECT_EQ(result.reason_code(), 0);
}

class HostnameTest : public testing::Test {
 protected:
  void Try(std::string arg, std::unique_ptr<Policy> policy) {
    const std::string path = GetTestSourcePath("sandbox2/testcases/hostname");
    std::vector<std::string> args = {path, std::move(arg)};
    auto executor = absl::make_unique<Executor>(path, args);
    Sandbox2 sandbox(std::move(executor), std::move(policy));
    auto result = sandbox.Run();
    ASSERT_EQ(result.final_status(), Result::OK);
    code_ = result.reason_code();
  }

  int code_;
};

TEST_F(HostnameTest, None) {
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, PolicyBuilder()
                                        .DisableNamespaces()
                                        // Don't restrict the syscalls at all
                                        .DangerDefaultAllowAll()
                                        .TryBuild());
  Try("sandbox2", std::move(policy));
  EXPECT_EQ(code_, 1);
}

TEST_F(HostnameTest, Default) {
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, PolicyBuilder()
                                        // Don't restrict the syscalls at all
                                        .DangerDefaultAllowAll()
                                        .TryBuild());
  Try("sandbox2", std::move(policy));
  EXPECT_EQ(code_, 0);
}

TEST_F(HostnameTest, Configured) {
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, PolicyBuilder()
                                        // Don't restrict the syscalls at all
                                        .DangerDefaultAllowAll()
                                        .SetHostname("configured")
                                        .TryBuild());
  Try("configured", std::move(policy));
  EXPECT_EQ(code_, 0);
}

}  // namespace
}  // namespace sandbox2
