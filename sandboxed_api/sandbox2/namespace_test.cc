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

#include "sandboxed_api/sandbox2/namespace.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <syscall.h>
#include <unistd.h>

#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/allow_all_syscalls.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sandboxed_api/util/temp_file.h"

namespace sandbox2 {
namespace {

namespace file_util = ::sapi::file_util;
using ::sapi::CreateDefaultPermissiveTestPolicy;
using ::sapi::CreateNamedTempFile;
using ::sapi::GetTestSourcePath;
using ::sapi::GetTestTempPath;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Ne;
using ::testing::SizeIs;
using ::testing::StartsWith;
using ::testing::StrEq;  // sapi::google3-only(broken matchers)

std::string GetTestcaseBinPath(absl::string_view bin_name) {
  return GetTestSourcePath(absl::StrCat("sandbox2/testcases/", bin_name));
}

std::vector<std::string> RunSandboxeeWithArgsAndPolicy(
    const std::string& bin_path, std::initializer_list<std::string> args,
    std::unique_ptr<Policy> policy = nullptr) {
  if (!policy) {
    policy = CreateDefaultPermissiveTestPolicy(bin_path).BuildOrDie();
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

TEST(NamespaceTest, FileNamespaceWorks) {
  // Mount /binary_path RO and check that it exists and is readable.
  // /etc/passwd should not exist.

  const std::string path = GetTestcaseBinPath("namespace");
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, CreateDefaultPermissiveTestPolicy(path)
                                             .AddFileAt(path, "/binary_path")
                                             .TryBuild());
  std::vector<std::string> result = RunSandboxeeWithArgsAndPolicy(
      path, {path, "0", "/binary_path", "/etc/passwd"}, std::move(policy));
  EXPECT_THAT(result, ElementsAre("/binary_path"));
}

TEST(NamespaceTest, ReadOnlyIsRespected) {
  // Mount temporary file as RO and check that it actually is RO.
  auto [name, fd] = CreateNamedTempFile(GetTestTempPath("temp_file")).value();
  file_util::fileops::FDCloser temp_closer(fd);

  const std::string path = GetTestcaseBinPath("namespace");
  {
    SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                              CreateDefaultPermissiveTestPolicy(path)
                                  .AddFileAt(name, "/temp_file")
                                  .TryBuild());
    // Check that it is readable
    std::vector<std::string> result = RunSandboxeeWithArgsAndPolicy(
        path, {path, "0", "/temp_file"}, std::move(policy));
    EXPECT_THAT(result, ElementsAre("/temp_file"));
  }
  {
    SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                              CreateDefaultPermissiveTestPolicy(path)
                                  .AddFileAt(name, "/temp_file")
                                  .TryBuild());
    // Now check that it is not writeable
    std::vector<std::string> result = RunSandboxeeWithArgsAndPolicy(
        path, {path, "1", "/temp_file"}, std::move(policy));
    EXPECT_THAT(result, IsEmpty());
  }
}

TEST(NamespaceTest, UserNamespaceWorks) {
  const std::string path = GetTestcaseBinPath("namespace");

  // Check that getpid() returns 2 (which is the case inside pid NS).
  {
    std::vector<std::string> result =
        RunSandboxeeWithArgsAndPolicy(path, {path, "2"});
    EXPECT_THAT(result, ElementsAre("2"));
  }

  // Validate that getpid() does not return 2 when outside of a pid NS.
  {
    std::vector<std::string> result = RunSandboxeeWithArgsAndPolicy(
        path, {path, "2"},
        PolicyBuilder()
            .DisableNamespaces()
            .DefaultAction(AllowAllSyscalls())  // Do not restrict syscalls
            .BuildOrDie());
    EXPECT_THAT(result, ElementsAre(Ne("2")));
  }
}

TEST(NamespaceTest, UserNamespaceIDMapWritten) {
  // Check that the idmap is initialized before the sandbox application is
  // started.
  const std::string path = GetTestcaseBinPath("namespace");
  {
    std::vector<std::string> result =
        RunSandboxeeWithArgsAndPolicy(path, {path, "3", "1000", "1000"});
    EXPECT_THAT(result, ElementsAre("1000", "1000"));
  }

  // Check that the uid/gid is the same when not using namespaces.
  {
    std::vector<std::string> result = RunSandboxeeWithArgsAndPolicy(
        path, {path, "3"},
        PolicyBuilder()
            .DisableNamespaces()
            .DefaultAction(AllowAllSyscalls())  // Do not restrict syscalls
            .BuildOrDie());
    EXPECT_THAT(result,
                ElementsAre(absl::StrCat(getuid()), absl::StrCat(getgid())));
  }
}

TEST(NamespaceTest, RootReadOnly) {
  // Mount rw tmpfs at /tmp and check it is RW.
  // Check also that / is RO.
  const std::string path = GetTestcaseBinPath("namespace");
  SAPI_ASSERT_OK_AND_ASSIGN(
      auto policy, CreateDefaultPermissiveTestPolicy(path)
                       .AddTmpfs("/tmp", /*size=*/4ULL << 20 /* 4 MiB */)
                       .TryBuild());
  std::vector<std::string> result = RunSandboxeeWithArgsAndPolicy(
      path, {path, "4", "/tmp/testfile", "/testfile"}, std::move(policy));
  EXPECT_THAT(result, ElementsAre("/tmp/testfile"));
}

TEST(NamespaceTest, RootWritable) {
  // Mount root rw and check it
  const std::string path = GetTestcaseBinPath("namespace");
  SAPI_ASSERT_OK_AND_ASSIGN(
      auto policy,
      CreateDefaultPermissiveTestPolicy(path).SetRootWritable().TryBuild());
  std::vector<std::string> result = RunSandboxeeWithArgsAndPolicy(
      path, {path, "4", "/testfile"}, std::move(policy));
  EXPECT_THAT(result, ElementsAre("/testfile"));
}

TEST(NamespaceTest, HostnameNone) {
  const std::string path = GetTestcaseBinPath("namespace");
  std::vector<std::string> result = RunSandboxeeWithArgsAndPolicy(
      path, {path, "7"},
      PolicyBuilder()
          .DisableNamespaces()
          .DefaultAction(AllowAllSyscalls())  // Do not restrict syscalls
          .BuildOrDie());
  EXPECT_THAT(result, ElementsAre(Ne("sandbox2")));
}

TEST(NamespaceTest, HostnameDefault) {
  const std::string path = GetTestcaseBinPath("namespace");
  std::vector<std::string> result =
      RunSandboxeeWithArgsAndPolicy(path, {path, "7"});
  EXPECT_THAT(result, ElementsAre("sandbox2"));
}

TEST(NamespaceTest, HostnameConfigured) {
  const std::string path = GetTestcaseBinPath("namespace");
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, CreateDefaultPermissiveTestPolicy(path)
                                             .SetHostname("configured")
                                             .TryBuild());
  std::vector<std::string> result =
      RunSandboxeeWithArgsAndPolicy(path, {path, "7"}, std::move(policy));
  EXPECT_THAT(result, ElementsAre("configured"));
}

TEST(NamespaceTest, TestInterfacesNoNetwork) {
  const std::string path = GetTestcaseBinPath("namespace");
  std::vector<std::string> result =
      RunSandboxeeWithArgsAndPolicy(path, {path, "5"});
  // Only loopback network interface 'lo'.
  EXPECT_THAT(result, ElementsAre("lo"));
}

TEST(NamespaceTest, TestInterfacesWithNetwork) {
  const std::string path = GetTestcaseBinPath("namespace");
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, CreateDefaultPermissiveTestPolicy(path)
                                             .AllowUnrestrictedNetworking()
                                             .TryBuild());

  std::vector<std::string> result =
      RunSandboxeeWithArgsAndPolicy(path, {path, "5"}, std::move(policy));
  // Loopback network interface 'lo' and more.
  EXPECT_THAT(result, Contains("lo"));
  EXPECT_THAT(result, SizeIs(Gt(1)));
}

}  // namespace
}  // namespace sandbox2
