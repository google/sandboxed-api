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

#include "sandboxed_api/sandbox2/stack_trace.h"

#include <dirent.h>

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/cleanup/cleanup.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/flags/reflection.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/global_forkclient.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sandboxed_api/util/temp_file.h"

ABSL_DECLARE_FLAG(bool, sandbox_libunwind_crash_handler);

namespace sandbox2 {
namespace {

namespace file_util = ::sapi::file_util;
using ::sapi::CreateNamedTempFileAndClose;
using ::sapi::GetTestSourcePath;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::Not;

// Test that symbolization of stack traces works.
void SymbolizationWorksCommon(
    const std::function<void(PolicyBuilder*)>& modify_policy) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/symbolize");
  std::vector<std::string> args = {path, "1"};

  SAPI_ASSERT_OK_AND_ASSIGN(std::string temp_filename,
                            CreateNamedTempFileAndClose("/tmp/"));
  absl::Cleanup temp_cleanup = [&temp_filename] {
    remove(temp_filename.c_str());
  };
  ASSERT_THAT(
      file_util::fileops::CopyFile("/proc/cpuinfo", temp_filename, 0444),
      IsTrue());

  auto policybuilder = PolicyBuilder()
                           // Don't restrict the syscalls at all.
                           .DangerDefaultAllowAll()
                           .AddFile(path)
                           .AddLibrariesForBinary(path)
                           .AddFileAt(temp_filename, "/proc/cpuinfo");
  modify_policy(&policybuilder);
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, policybuilder.TryBuild());

  Sandbox2 s2(std::make_unique<Executor>(path, args), std::move(policy));
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::SIGNALED));
  ASSERT_THAT(result.GetStackTrace(), HasSubstr("CrashMe"));
  // Check that demangling works as well.
  ASSERT_THAT(result.GetStackTrace(), HasSubstr("CrashMe()"));
}

TEST(StackTraceTest, SymbolizationWorksNonSandboxedLibunwind) {
  SKIP_SANITIZERS_AND_COVERAGE;
  absl::FlagSaver fs;
  absl::SetFlag(&FLAGS_sandbox_libunwind_crash_handler, false);

  SymbolizationWorksCommon([](PolicyBuilder*) {});
}

TEST(StackTraceTest, SymbolizationWorksSandboxedLibunwind) {
  SKIP_SANITIZERS_AND_COVERAGE;
  absl::FlagSaver fs;
  absl::SetFlag(&FLAGS_sandbox_libunwind_crash_handler, true);

  SymbolizationWorksCommon([](PolicyBuilder*) {});
}

TEST(StackTraceTest, SymbolizationWorksSandboxedLibunwindProcDirMounted) {
  SKIP_SANITIZERS_AND_COVERAGE;
  absl::FlagSaver fs;
  absl::SetFlag(&FLAGS_sandbox_libunwind_crash_handler, true);

  SymbolizationWorksCommon(
      [](PolicyBuilder* builder) { builder->AddDirectory("/proc"); });
}

TEST(StackTraceTest, SymbolizationWorksSandboxedLibunwindProcFileMounted) {
  SKIP_SANITIZERS_AND_COVERAGE;
  absl::FlagSaver fs;
  absl::SetFlag(&FLAGS_sandbox_libunwind_crash_handler, true);

  SymbolizationWorksCommon([](PolicyBuilder* builder) {
    builder->AddFile("/proc/sys/vm/overcommit_memory");
  });
}

TEST(StackTraceTest, SymbolizationWorksSandboxedLibunwindSysDirMounted) {
  SKIP_SANITIZERS_AND_COVERAGE;
  absl::FlagSaver fs;
  absl::SetFlag(&FLAGS_sandbox_libunwind_crash_handler, true);

  SymbolizationWorksCommon(
      [](PolicyBuilder* builder) { builder->AddDirectory("/sys"); });
}

TEST(StackTraceTest, SymbolizationWorksSandboxedLibunwindSysFileMounted) {
  SKIP_SANITIZERS_AND_COVERAGE;
  absl::FlagSaver fs;
  absl::SetFlag(&FLAGS_sandbox_libunwind_crash_handler, true);

  SymbolizationWorksCommon([](PolicyBuilder* builder) {
    builder->AddFile("/sys/devices/system/cpu/online");
  });
}

static size_t FileCountInDirectory(const std::string& path) {
  std::vector<std::string> fds;
  std::string error;
  CHECK(file_util::fileops::ListDirectoryEntries(path, &fds, &error));
  return fds.size();
}

TEST(StackTraceTest, ForkEnterNsLibunwindDoesNotLeakFDs) {
  SKIP_SANITIZERS_AND_COVERAGE;
  absl::FlagSaver fs;
  absl::SetFlag(&FLAGS_sandbox_libunwind_crash_handler, true);

  // Very first sanitization might create some fds (e.g. for initial
  // namespaces).
  SymbolizationWorksCommon([](PolicyBuilder* builder) {
    builder->AddFile("/sys/devices/system/cpu/online");
  });

  // Get list of open FDs in the global forkserver.
  pid_t forkserver_pid = GlobalForkClient::GetPid();
  std::string forkserver_fd_path =
      absl::StrCat("/proc/", forkserver_pid, "/fd");
  size_t filecount_before = FileCountInDirectory(forkserver_fd_path);

  SymbolizationWorksCommon([](PolicyBuilder* builder) {
    builder->AddFile("/sys/devices/system/cpu/online");
  });

  EXPECT_THAT(filecount_before, Eq(FileCountInDirectory(forkserver_fd_path)));
}

// Test that symbolization skips writeable files (attack vector).
TEST(StackTraceTest, SymbolizationTrustedFilesOnly) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path = GetTestSourcePath("sandbox2/testcases/symbolize");
  std::vector<std::string> args = {path, "2"};

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            PolicyBuilder()
                                // Don't restrict the syscalls at all.
                                .DangerDefaultAllowAll()
                                .AddFile(path)
                                .AddLibrariesForBinary(path)
                                .TryBuild());
  Sandbox2 s2(std::make_unique<Executor>(path, args), std::move(policy));
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::SIGNALED));
  ASSERT_THAT(result.GetStackTrace(), Not(HasSubstr("CrashMe")));
}

TEST(StackTraceTest, CompactStackTrace) {
  EXPECT_THAT(CompactStackTrace({}), IsEmpty());
  EXPECT_THAT(CompactStackTrace({"_start"}), ElementsAre("_start"));
  EXPECT_THAT(CompactStackTrace({
                  "_start",
                  "main",
                  "recursive_call",
                  "recursive_call",
                  "recursive_call",
                  "tail_call",
              }),
              ElementsAre("_start", "main", "recursive_call",
                          "(previous frame repeated 2 times)", "tail_call"));
  EXPECT_THAT(CompactStackTrace({
                  "_start",
                  "main",
                  "recursive_call",
                  "recursive_call",
                  "recursive_call",
                  "recursive_call",
              }),
              ElementsAre("_start", "main", "recursive_call",
                          "(previous frame repeated 3 times)"));
}

}  // namespace
}  // namespace sandbox2
