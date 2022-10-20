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
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/comms.h"
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
using ::sapi::CreateNamedTempFile;
using ::sapi::GetTestSourcePath;
using ::sapi::GetTestTempPath;
using ::testing::Eq;
using ::testing::Ne;

int RunSandboxeeWithArgsAndPolicy(const std::string& sandboxee,
                                  std::initializer_list<std::string> args,
                                  std::unique_ptr<Policy> policy) {
  Sandbox2 sandbox(std::make_unique<Executor>(sandboxee, args),
                   std::move(policy));

  Result result = sandbox.Run();
  EXPECT_THAT(result.final_status(), Eq(Result::OK));
  return result.reason_code();
}

constexpr absl::string_view kNamespaceTestBinary =
    "sandbox2/testcases/namespace";
constexpr absl::string_view kHostnameTestBinary = "sandbox2/testcases/hostname";

TEST(NamespaceTest, FileNamespaceWorks) {
  // Mount /binary_path RO and check that it exists and is readable.
  // /etc/passwd should not exist.

  const std::string path = GetTestSourcePath(kNamespaceTestBinary);
  int reason_code = RunSandboxeeWithArgsAndPolicy(
      path, {path, "0", "/binary_path", "/etc/passwd"},
      PolicyBuilder()
          .DangerDefaultAllowAll()  // Do not restrict syscalls
          .AddFileAt(path, "/binary_path")
          .BuildOrDie());
  EXPECT_THAT(reason_code, Eq(2));
}

TEST(NamespaceTest, ReadOnlyIsRespected) {
  // Mount temporary file as RO and check that it actually is RO.
  auto [name, fd] = CreateNamedTempFile(GetTestTempPath("temp_file")).value();
  file_util::fileops::FDCloser temp_closer(fd);

  const std::string path = GetTestSourcePath(kNamespaceTestBinary);
  {
    // Check that it is readable
    int reason_code = RunSandboxeeWithArgsAndPolicy(
        path, {path, "0", "/temp_file"},
        PolicyBuilder()
            .DangerDefaultAllowAll()  // Do not restrict syscalls
            .AddFileAt(name, "/temp_file")
            .BuildOrDie());
    EXPECT_THAT(reason_code, Eq(0));
  }
  {
    // Now check that it is not writeable
    int reason_code = RunSandboxeeWithArgsAndPolicy(
        path, {path, "1", "/temp_file"},
        PolicyBuilder()
            .DangerDefaultAllowAll()  // Do not restrict syscalls
            .AddFileAt(name, "/temp_file")
            .BuildOrDie());
    EXPECT_THAT(reason_code, Eq(1));
  }
}

TEST(NamespaceTest, UserNamespaceWorks) {
  const std::string path = GetTestSourcePath(kNamespaceTestBinary);

  // Check that getpid() returns 2 (which is the case inside pid NS).
  {
    int reason_code = RunSandboxeeWithArgsAndPolicy(
        path, {path, "2"},
        PolicyBuilder()
            .DangerDefaultAllowAll()  // Do not restrict syscalls
            .BuildOrDie());
    EXPECT_THAT(reason_code, Eq(0));
  }

  // Validate that getpid() does not return 2 when outside of a pid NS.
  {
    int reason_code = RunSandboxeeWithArgsAndPolicy(
        path, {path, "2"},
        PolicyBuilder()
            .DisableNamespaces()
            .DangerDefaultAllowAll()  // Do not restrict syscalls
            .BuildOrDie());
    EXPECT_THAT(reason_code, Ne(0));
  }
}

TEST(NamespaceTest, UserNamespaceIDMapWritten) {
  // Check that the idmap is initialized before the sandbox application is
  // started.
  const std::string path = GetTestSourcePath(kNamespaceTestBinary);
  {
    int reason_code = RunSandboxeeWithArgsAndPolicy(
        path, {path, "3", "1000", "1000"},
        PolicyBuilder()
            .DangerDefaultAllowAll()  // Do not restrict syscalls
            .BuildOrDie());
    EXPECT_THAT(reason_code, Eq(0));
  }

  // Check that the uid/gid is the same when not using namespaces.
  {
    int reason_code = RunSandboxeeWithArgsAndPolicy(
        path, {path, "3", absl::StrCat(getuid()), absl::StrCat(getgid())},
        PolicyBuilder()
            .DisableNamespaces()
            .DangerDefaultAllowAll()  // Do not restrict syscalls
            .BuildOrDie());
    EXPECT_THAT(reason_code, Eq(0));
  }
}

TEST(NamespaceTest, RootReadOnly) {
  // Mount rw tmpfs at /tmp and check it is RW.
  // Check also that / is RO.
  const std::string path = GetTestSourcePath(kNamespaceTestBinary);
  int reason_code = RunSandboxeeWithArgsAndPolicy(
      path, {path, "4", "/tmp/testfile", "/testfile"},
      PolicyBuilder()
          .DangerDefaultAllowAll()  // Do not restrict syscalls
          .AddTmpfs("/tmp", /*size=*/4ULL << 20 /* 4 MiB */)
          .BuildOrDie());
  EXPECT_THAT(reason_code, Eq(2));
}

TEST(NamespaceTest, RootWritable) {
  // Mount root rw and check it
  const std::string path = GetTestSourcePath(kNamespaceTestBinary);
  int reason_code = RunSandboxeeWithArgsAndPolicy(
      path, {path, "4", "/testfile"},
      PolicyBuilder()
          .DangerDefaultAllowAll()  // Do not restrict syscalls
          .SetRootWritable()
          .BuildOrDie());
  EXPECT_THAT(reason_code, Eq(0));
}

TEST(HostnameTest, None) {
  const std::string path = GetTestSourcePath(kHostnameTestBinary);
  int reason_code = RunSandboxeeWithArgsAndPolicy(
      path, {path, "sandbox2"},
      PolicyBuilder()
          .DisableNamespaces()
          .DangerDefaultAllowAll()  // Do not restrict syscalls
          .BuildOrDie());
  EXPECT_THAT(reason_code, Eq(1));
}

TEST(HostnameTest, Default) {
  const std::string path = GetTestSourcePath(kHostnameTestBinary);
  int reason_code = RunSandboxeeWithArgsAndPolicy(
      path, {path, "sandbox2"},
      PolicyBuilder()
          .DangerDefaultAllowAll()  // Do not restrict syscalls
          .BuildOrDie());
  EXPECT_THAT(reason_code, Eq(0));
}

TEST(HostnameTest, Configured) {
  const std::string path = GetTestSourcePath(kHostnameTestBinary);
  int reason_code = RunSandboxeeWithArgsAndPolicy(
      path, {path, "configured"},
      PolicyBuilder()
          .DangerDefaultAllowAll()  // Do not restrict syscalls
          .SetHostname("configured")
          .BuildOrDie());
  EXPECT_THAT(reason_code, Eq(0));
}

}  // namespace
}  // namespace sandbox2
