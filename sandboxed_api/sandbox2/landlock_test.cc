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

#include "sandboxed_api/sandbox2/landlock.h"

#include <asm-generic/unistd.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/allowlists/enable_landlock.h"
#include "sandboxed_api/sandbox2/allowlists/unrestricted_networking.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/fork_client.h"
#include "sandboxed_api/sandbox2/global_forkclient.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/temp_file.h"

namespace sandbox2 {
namespace {

using ::sapi::CreateDefaultPermissiveTestPolicy;
using ::sapi::GetTestSourcePath;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::NotNull;
using ::testing::SizeIs;

std::string GetTestcaseBinPath(absl::string_view bin_name) {
  return GetTestSourcePath(absl::StrCat("sandbox2/testcases/", bin_name));
}

PolicyBuilder CreateLandlockPermissiveTestPolicy(absl::string_view bin_path) {
  return CreateDefaultPermissiveTestPolicy(bin_path)
      .AddFile(bin_path)
      .EnableLandlock(sandbox2::EnableLandlock());
}

PolicyBuilder CreateLandlockTestPolicy(absl::string_view bin_path) {
  return CreateLandlockPermissiveTestPolicy(bin_path);
}

std::vector<std::string> RunSandboxeeWithArgsAndPolicy(
    const std::string& bin_path, std::initializer_list<std::string> args,
    std::unique_ptr<Policy> policy = nullptr) {
  if (!policy) {
    policy = CreateLandlockPermissiveTestPolicy(bin_path).BuildOrDie();
  }
  Sandbox2 sandbox(std::make_unique<Executor>(bin_path, args),
                   std::move(policy));

  CHECK(sandbox.RunAsync());
  Comms* comms = sandbox.comms();
  uint64_t num;

  std::vector<std::string> entries;
  if (comms->RecvUint64(&num)) {
    entries.reserve(num);
    for (int i = 0; i < num; ++i) {
      std::string entry;
      CHECK(comms->RecvString(&entry));
      entries.push_back(std::move(entry));
    }
  }
  Result result = sandbox.AwaitResult();
  EXPECT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));
  return entries;
}

class LandlockTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!IsLandlockSupported()) {
      GTEST_SKIP() << "Landlock not supported on this kernel";
    }
    GlobalForkClient::Shutdown();
  }
};

TEST_F(LandlockTest, LandlockWithNetworkNamespace) {
  const std::string path = GetTestcaseBinPath("namespace");

  // By default, CLONE_NEWNET is used so only loopback 'lo' interface exists.
  SAPI_ASSERT_OK_AND_ASSIGN(
      auto policy_isolated,
      CreateLandlockPermissiveTestPolicy(path).TryBuild());
  std::vector<std::string> result_isolated = RunSandboxeeWithArgsAndPolicy(
      path, {path, "5"}, std::move(policy_isolated));
  EXPECT_THAT(result_isolated, ElementsAre("lo"));

  // With UnrestrictedNetworking(), CLONE_NEWNET is disabled so host interfaces
  // are accessible.
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy_host,
                            CreateLandlockPermissiveTestPolicy(path)
                                .Allow(UnrestrictedNetworking())
                                .TryBuild());
  std::vector<std::string> result_host =
      RunSandboxeeWithArgsAndPolicy(path, {path, "5"}, std::move(policy_host));
  EXPECT_THAT(result_host, Contains("lo"));
  EXPECT_THAT(result_host, SizeIs(testing::Gt(1)));
}

TEST_F(LandlockTest, LandlockProcessIsolation) {
  const std::string path = GetTestcaseBinPath("namespace");

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy1,
                            CreateLandlockPermissiveTestPolicy(path)
                                .AddFile(path)
                                .AddDirectory("/proc", true)
                                .TryBuild());

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy2,
                            CreateLandlockPermissiveTestPolicy(path)
                                .AddFile(path)
                                .AddDirectory("/proc", true)
                                .TryBuild());

  Sandbox2 sandbox1(
      std::make_unique<Executor>(path, std::vector<std::string>{path, "11"}),
      std::move(policy1));
  Sandbox2 sandbox2(
      std::make_unique<Executor>(path, std::vector<std::string>{path, "11"}),
      std::move(policy2));

  CHECK(sandbox1.RunAsync());
  CHECK(sandbox2.RunAsync());

  auto get_result = [](Sandbox2* sandbox) {
    Comms* comms = sandbox->comms();
    uint64_t num;
    std::vector<std::string> entries;
    if (comms->RecvUint64(&num)) {
      entries.reserve(num);
      for (int i = 0; i < num; ++i) {
        std::string entry;
        CHECK(comms->RecvString(&entry));
        entries.push_back(std::move(entry));
      }
    }
    return entries;
  };

  std::vector<std::string> res1 = get_result(&sandbox1);
  std::vector<std::string> res2 = get_result(&sandbox2);

  // Unblock the sandboxees so they can exit.
  uint32_t ack = 1;
  sandbox1.comms()->SendUint32(ack);
  sandbox2.comms()->SendUint32(ack);

  EXPECT_THAT(sandbox1.AwaitResult().final_status(), Eq(Result::OK));
  EXPECT_THAT(sandbox2.AwaitResult().final_status(), Eq(Result::OK));

  EXPECT_THAT(res1, ElementsAre("2"));
  EXPECT_THAT(res2, ElementsAre("3"));
  EXPECT_NE(res1, res2);
}

TEST_F(LandlockTest, CustomForkserverWorks) {
  const std::string path = GetTestcaseBinPath("custom_fork");

  // Test 1: Opening an allowlisted file (the binary itself) succeeds.
  auto fork_executor_ok =
      std::make_unique<Executor>(path, std::vector<std::string>{path, path});
  std::unique_ptr<ForkClient> fork_client_ok =
      fork_executor_ok->StartForkServer();
  ASSERT_THAT(fork_client_ok.get(), NotNull());

  SAPI_ASSERT_OK_AND_ASSIGN(
      auto policy_ok,
      CreateLandlockPermissiveTestPolicy(path).AddFile(path).TryBuild());
  Sandbox2 sandbox_ok(std::make_unique<Executor>(fork_client_ok.get()),
                      std::move(policy_ok));
  ASSERT_TRUE(sandbox_ok.RunAsync());
  Result result_ok = sandbox_ok.AwaitResult();
  EXPECT_THAT(result_ok.final_status(), Eq(Result::OK));
  EXPECT_THAT(result_ok.reason_code(), Eq(0));

  // Test 2: Opening an unlisted file (/etc/passwd) is blocked by Landlock.
  auto fork_executor_blocked = std::make_unique<Executor>(
      path, std::vector<std::string>{path, "/etc/passwd"});
  std::unique_ptr<ForkClient> fork_client_blocked =
      fork_executor_blocked->StartForkServer();
  ASSERT_THAT(fork_client_blocked.get(), NotNull());

  SAPI_ASSERT_OK_AND_ASSIGN(
      auto policy_blocked,
      CreateLandlockPermissiveTestPolicy(path).AddFile(path).TryBuild());
  Sandbox2 sandbox_blocked(
      std::make_unique<Executor>(fork_client_blocked.get()),
      std::move(policy_blocked));
  ASSERT_TRUE(sandbox_blocked.RunAsync());
  Result result_blocked = sandbox_blocked.AwaitResult();
  EXPECT_THAT(result_blocked.final_status(), Eq(Result::OK));
  EXPECT_THAT(result_blocked.reason_code(), Eq(1));
}

TEST_F(LandlockTest, LandlockSignalScoping_BlocksSignalingForkserver) {
  const std::string path = GetTestcaseBinPath("namespace");

  // 1. In Landlock mode, attempting to signal PID 1 (forkserver outside domain)
  // fails with EPERM due to LANDLOCK_SCOPE_SIGNAL!
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy_landlock_1,
                            CreateLandlockTestPolicy(path).TryBuild());
  std::vector<std::string> res_landlock_1 = RunSandboxeeWithArgsAndPolicy(
      path, {path, "12", "1"}, std::move(policy_landlock_1));
  EXPECT_THAT(res_landlock_1, ElementsAre("kill_failed:EPERM"));

  // 2. In Namespace mode, PID 1 inside the private PID namespace is our own
  // container init process, so signaling PID 1 succeeds!
  SAPI_ASSERT_OK_AND_ASSIGN(
      auto policy_ns_1,
      CreateDefaultPermissiveTestPolicy(path).AddFile(path).TryBuild());
  std::vector<std::string> res_ns_1 = RunSandboxeeWithArgsAndPolicy(
      path, {path, "12", "1"}, std::move(policy_ns_1));
  EXPECT_THAT(res_ns_1, ElementsAre("kill_success"));

  // 3. Interleaving Landlock mode again confirming signal scoping is
  // re-applied.
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy_landlock_2,
                            CreateLandlockTestPolicy(path).TryBuild());
  std::vector<std::string> res_landlock_2 = RunSandboxeeWithArgsAndPolicy(
      path, {path, "12", "1"}, std::move(policy_landlock_2));
  EXPECT_THAT(res_landlock_2, ElementsAre("kill_failed:EPERM"));

  // 4. Interleaving Namespace mode again.
  SAPI_ASSERT_OK_AND_ASSIGN(
      auto policy_ns_2,
      CreateDefaultPermissiveTestPolicy(path).AddFile(path).TryBuild());
  std::vector<std::string> res_ns_2 = RunSandboxeeWithArgsAndPolicy(
      path, {path, "12", "1"}, std::move(policy_ns_2));
  EXPECT_THAT(res_ns_2, ElementsAre("kill_success"));
}

TEST_F(LandlockTest, InterleavedNamespaceAndLandlockRequests) {
  const std::string path = GetTestcaseBinPath("namespace");

  auto get_result = [](Sandbox2* sandbox) {
    Comms* comms = sandbox->comms();
    uint64_t num;
    std::vector<std::string> entries;
    if (comms->RecvUint64(&num)) {
      entries.reserve(num);
      for (int i = 0; i < num; ++i) {
        std::string entry;
        CHECK(comms->RecvString(&entry));
        entries.push_back(std::move(entry));
      }
    }
    return entries;
  };

  // 1. Run a request with Landlock enabled.
  SAPI_ASSERT_OK_AND_ASSIGN(
      auto policy_landlock_1,
      CreateLandlockTestPolicy(path).AddDirectory("/proc", true).TryBuild());
  Sandbox2 sandbox_landlock_1(
      std::make_unique<Executor>(path, std::vector<std::string>{path, "11"}),
      std::move(policy_landlock_1));
  ASSERT_TRUE(sandbox_landlock_1.RunAsync());
  std::vector<std::string> res_landlock_1 = get_result(&sandbox_landlock_1);
  sandbox_landlock_1.comms()->SendUint32(1);
  EXPECT_THAT(sandbox_landlock_1.AwaitResult().final_status(), Eq(Result::OK));

  // Verify we are strictly in Landlock mode (shared PID namespace: no PID 1).
  ASSERT_THAT(res_landlock_1, SizeIs(1));
  EXPECT_NE(res_landlock_1[0], "1");

  // 2. Run a request in traditional Namespace mode.
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy_ns_1,
                            CreateDefaultPermissiveTestPolicy(path)
                                .AddFile(path)
                                .AddDirectory("/proc", true)
                                .TryBuild());
  Sandbox2 sandbox_ns_1(
      std::make_unique<Executor>(path, std::vector<std::string>{path, "11"}),
      std::move(policy_ns_1));
  ASSERT_TRUE(sandbox_ns_1.RunAsync());
  std::vector<std::string> res_ns_1 = get_result(&sandbox_ns_1);
  sandbox_ns_1.comms()->SendUint32(1);
  EXPECT_THAT(sandbox_ns_1.AwaitResult().final_status(), Eq(Result::OK));

  // Verify we are strictly in Namespace mode (private PID namespace: sees PID
  // 1).
  EXPECT_THAT(res_ns_1, SizeIs(testing::Gt(1)));
  EXPECT_THAT(res_ns_1, Contains("1"));

  // 3. Keep interleaving: Landlock mode again.
  SAPI_ASSERT_OK_AND_ASSIGN(
      auto policy_landlock_2,
      CreateLandlockTestPolicy(path).AddDirectory("/proc", true).TryBuild());
  Sandbox2 sandbox_landlock_2(
      std::make_unique<Executor>(path, std::vector<std::string>{path, "11"}),
      std::move(policy_landlock_2));
  ASSERT_TRUE(sandbox_landlock_2.RunAsync());
  std::vector<std::string> res_landlock_2 = get_result(&sandbox_landlock_2);
  sandbox_landlock_2.comms()->SendUint32(1);
  EXPECT_THAT(sandbox_landlock_2.AwaitResult().final_status(), Eq(Result::OK));
  ASSERT_THAT(res_landlock_2, SizeIs(1));
  EXPECT_NE(res_landlock_2[0], "1");

  // 4. Namespace mode again.
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy_ns_2,
                            CreateDefaultPermissiveTestPolicy(path)
                                .AddFile(path)
                                .AddDirectory("/proc", true)
                                .TryBuild());
  Sandbox2 sandbox_ns_2(
      std::make_unique<Executor>(path, std::vector<std::string>{path, "11"}),
      std::move(policy_ns_2));
  ASSERT_TRUE(sandbox_ns_2.RunAsync());
  std::vector<std::string> res_ns_2 = get_result(&sandbox_ns_2);
  sandbox_ns_2.comms()->SendUint32(1);
  EXPECT_THAT(sandbox_ns_2.AwaitResult().final_status(), Eq(Result::OK));
  EXPECT_THAT(res_ns_2, SizeIs(testing::Gt(1)));
  EXPECT_THAT(res_ns_2, Contains("1"));
}

TEST_F(LandlockTest, InterleavedFilesystemIsolation_NamespaceAndLandlock) {
  const std::string path = GetTestcaseBinPath("namespace");

  // 1. Run in Landlock mode: /etc/passwd is blocked by Landlock rules.
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy_landlock_1,
                            CreateLandlockTestPolicy(path).TryBuild());
  std::vector<std::string> res_landlock_1 = RunSandboxeeWithArgsAndPolicy(
      path, {path, "9", path, "/etc/passwd"}, std::move(policy_landlock_1));
  EXPECT_THAT(res_landlock_1, ElementsAre(path));

  // 2. Run in traditional Namespace mode: /etc/passwd is omitted from mounts.
  SAPI_ASSERT_OK_AND_ASSIGN(
      auto policy_ns_1,
      CreateDefaultPermissiveTestPolicy(path).AddFile(path).TryBuild());
  std::vector<std::string> res_ns_1 = RunSandboxeeWithArgsAndPolicy(
      path, {path, "9", path, "/etc/passwd"}, std::move(policy_ns_1));
  EXPECT_THAT(res_ns_1, ElementsAre(path));

  // 3. Run in Landlock mode again explicitly confirming unmapped isolation.
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy_landlock_2,
                            CreateLandlockTestPolicy(path).TryBuild());
  std::vector<std::string> res_landlock_2 = RunSandboxeeWithArgsAndPolicy(
      path, {path, "9", path, "/etc/passwd"}, std::move(policy_landlock_2));
  EXPECT_THAT(res_landlock_2, ElementsAre(path));

  // 4. Run in traditional Namespace mode again.
  SAPI_ASSERT_OK_AND_ASSIGN(
      auto policy_ns_2,
      CreateDefaultPermissiveTestPolicy(path).AddFile(path).TryBuild());
  std::vector<std::string> res_ns_2 = RunSandboxeeWithArgsAndPolicy(
      path, {path, "9", path, "/etc/passwd"}, std::move(policy_ns_2));
  EXPECT_THAT(res_ns_2, ElementsAre(path));
}

TEST_F(LandlockTest, CustomForkserverInterleaved_FilesystemIsolation) {
  const std::string path = GetTestcaseBinPath("custom_fork");
  auto fork_executor = std::make_unique<Executor>(
      path, std::vector<std::string>{path, path, "/etc/passwd"});
  std::unique_ptr<ForkClient> fork_client = fork_executor->StartForkServer();
  ASSERT_THAT(fork_client.get(), NotNull());

  // 1. Request in Landlock mode where unmapped /etc/passwd triggers reason 1.
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy_landlock_1,
                            CreateLandlockTestPolicy(path).TryBuild());
  Sandbox2 sandbox_landlock_1(std::make_unique<Executor>(fork_client.get()),
                              std::move(policy_landlock_1));
  ASSERT_TRUE(sandbox_landlock_1.RunAsync());
  Result result_landlock_1 = sandbox_landlock_1.AwaitResult();
  EXPECT_THAT(result_landlock_1.final_status(), Eq(Result::OK));
  EXPECT_THAT(result_landlock_1.reason_code(), Eq(1));

  // 2. Request in traditional Namespace mode on EXACT SAME custom forkserver.
  SAPI_ASSERT_OK_AND_ASSIGN(
      auto policy_ns_1,
      CreateDefaultPermissiveTestPolicy(path).AddFile(path).TryBuild());
  Sandbox2 sandbox_ns_1(std::make_unique<Executor>(fork_client.get()),
                        std::move(policy_ns_1));
  ASSERT_TRUE(sandbox_ns_1.RunAsync());
  Result result_ns_1 = sandbox_ns_1.AwaitResult();
  EXPECT_THAT(result_ns_1.final_status(), Eq(Result::OK));
  EXPECT_THAT(result_ns_1.reason_code(), Eq(1));

  // 3. Request in Landlock mode again on the EXACT SAME custom forkserver.
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy_landlock_2,
                            CreateLandlockTestPolicy(path).TryBuild());
  Sandbox2 sandbox_landlock_2(std::make_unique<Executor>(fork_client.get()),
                              std::move(policy_landlock_2));
  ASSERT_TRUE(sandbox_landlock_2.RunAsync());
  Result result_landlock_2 = sandbox_landlock_2.AwaitResult();
  EXPECT_THAT(result_landlock_2.final_status(), Eq(Result::OK));
  EXPECT_THAT(result_landlock_2.reason_code(), Eq(1));

  // 4. Request in traditional Namespace mode again.
  SAPI_ASSERT_OK_AND_ASSIGN(
      auto policy_ns_2,
      CreateDefaultPermissiveTestPolicy(path).AddFile(path).TryBuild());
  Sandbox2 sandbox_ns_2(std::make_unique<Executor>(fork_client.get()),
                        std::move(policy_ns_2));
  ASSERT_TRUE(sandbox_ns_2.RunAsync());
  Result result_ns_2 = sandbox_ns_2.AwaitResult();
  EXPECT_THAT(result_ns_2.final_status(), Eq(Result::OK));
  EXPECT_THAT(result_ns_2.reason_code(), Eq(1));
}

TEST_F(LandlockTest, GlobalForkserverShutdownAndRestartWorks) {
  const std::string path = GetTestcaseBinPath("namespace");

  // Step 1: Host a sandboxee in Landlock mode (spawns the global forkserver).
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy_1,
                            CreateLandlockTestPolicy(path).TryBuild());
  Sandbox2 sandbox_1(
      std::make_unique<Executor>(path, std::vector<std::string>{path, "1"}),
      std::move(policy_1));
  ASSERT_TRUE(sandbox_1.RunAsync());
  Result result_1 = sandbox_1.AwaitResult();
  EXPECT_THAT(result_1.final_status(), Eq(Result::OK));
  EXPECT_THAT(result_1.reason_code(), Eq(0));

  // Step 2: Shutdown the global forkserver.
  GlobalForkClient::Shutdown();

  // Step 3: Start a new sandboxee (requires respawning the global forkserver).
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy_2,
                            CreateLandlockTestPolicy(path).TryBuild());
  Sandbox2 sandbox_2(
      std::make_unique<Executor>(path, std::vector<std::string>{path, "1"}),
      std::move(policy_2));
  ASSERT_TRUE(sandbox_2.RunAsync());
  Result result_2 = sandbox_2.AwaitResult();
  EXPECT_THAT(result_2.final_status(), Eq(Result::OK));
  EXPECT_THAT(result_2.reason_code(), Eq(0));
}

TEST_F(LandlockTest, LandlockTruncateAndReferAccess) {
  const std::string path = GetTestcaseBinPath("namespace");

  SAPI_ASSERT_OK_AND_ASSIGN(std::string rel_temp_dir,
                            sapi::CreateTempDir("landlock_test_"));
  std::string temp_dir = sapi::file_util::fileops::MakeAbsolute(
      rel_temp_dir, sapi::file_util::fileops::GetCWD());

  std::string dir1 = sapi::file::JoinPath(temp_dir, "dir1");
  std::string dir2 = sapi::file::JoinPath(temp_dir, "dir2");
  ASSERT_TRUE(sapi::file_util::fileops::CreateDirectoryRecursively(dir1, 0755));
  ASSERT_TRUE(sapi::file_util::fileops::CreateDirectoryRecursively(dir2, 0755));

  std::string file1 = sapi::file::JoinPath(dir1, "file1.txt");
  std::string file2 = sapi::file::JoinPath(dir2, "file2.txt");

  int fd = open(file1.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  ASSERT_GE(fd, 0);
  write(fd, "hello world", 11);
  close(fd);

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy_trunc,
                            CreateLandlockTestPolicy(path)
                                .AddDirectory(temp_dir, /*is_ro=*/false)
                                .TryBuild());

  // Test truncate on writable directory path
  std::vector<std::string> res_trunc = RunSandboxeeWithArgsAndPolicy(
      path, {path, "13", file1}, std::move(policy_trunc));
  EXPECT_THAT(res_trunc, ElementsAre("truncate_success"));

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy_refer,
                            CreateLandlockTestPolicy(path)
                                .AddDirectory(temp_dir, /*is_ro=*/false)
                                .TryBuild());

  // Test refer (rename/link) within writable directory
  std::vector<std::string> res_refer = RunSandboxeeWithArgsAndPolicy(
      path, {path, "14", file1, file2}, std::move(policy_refer));
  EXPECT_THAT(res_refer, ElementsAre("refer_success"));

  sapi::file_util::fileops::DeleteRecursively(temp_dir);
}

}  // namespace
}  // namespace sandbox2
