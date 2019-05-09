// Copyright 2019 Google LLC. All Rights Reserved.
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

#include <dirent.h>

#include <cstdio>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/flags/flag.h"
#include "absl/memory/memory.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/global_forkclient.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2/stack-trace.h"
#include "sandboxed_api/sandbox2/testing.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/sandbox2/util/temp_file.h"
#include "sandboxed_api/util/status_matchers.h"

using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Not;

namespace sandbox2 {
namespace {

// Temporarily overrides a flag, restores the original flag value when it goes
// out of scope.
template <typename T>
class TemporaryFlagOverride {
 public:
  using Flag = absl::Flag<T>;
  TemporaryFlagOverride(Flag* flag, T value)
      : flag_(flag), original_value_(absl::GetFlag(*flag)) {
    absl::SetFlag(flag, value);
  }

  ~TemporaryFlagOverride() { absl::SetFlag(flag_, original_value_); }

 private:
  Flag* flag_;
  T original_value_;
};

// Test that symbolization of stack traces works.
void SymbolizationWorksCommon(
    const std::function<void(PolicyBuilder*)>& modify_policy) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/symbolize");
  std::vector<std::string> args = {path, "1"};
  auto executor = absl::make_unique<Executor>(path, args);

  std::string temp_filename = CreateNamedTempFileAndClose("/tmp/").ValueOrDie();
  file_util::fileops::CopyFile("/proc/cpuinfo", temp_filename, 0444);
  struct TempCleanup {
    ~TempCleanup() { remove(capture->c_str()); }
    std::string* capture;
  } temp_cleanup{&temp_filename};

  PolicyBuilder policybuilder;
  policybuilder
      // Don't restrict the syscalls at all.
      .DangerDefaultAllowAll()
      .EnableNamespaces()
      .AddFile(path)
      .AddLibrariesForBinary(path)
      .AddFileAt(temp_filename, "/proc/cpuinfo");

  modify_policy(&policybuilder);
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, policybuilder.TryBuild());

  Sandbox2 s2(std::move(executor), std::move(policy));
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::SIGNALED));
  ASSERT_THAT(result.GetStackTrace(), HasSubstr("CrashMe"));
  // Check that demangling works as well.
  ASSERT_THAT(result.GetStackTrace(), HasSubstr("CrashMe()"));
}

TEST(StackTraceTest, SymbolizationWorksNonSandboxedLibunwind) {
  SKIP_SANITIZERS_AND_COVERAGE;
  TemporaryFlagOverride<bool> temp_override(
      &FLAGS_sandbox_libunwind_crash_handler, false);
  SymbolizationWorksCommon([](PolicyBuilder*) {});
}

TEST(StackTraceTest, SymbolizationWorksSandboxedLibunwind) {
  SKIP_SANITIZERS_AND_COVERAGE;
  TemporaryFlagOverride<bool> temp_override(
      &FLAGS_sandbox_libunwind_crash_handler, true);
  SymbolizationWorksCommon([](PolicyBuilder*) {});
}

TEST(StackTraceTest, SymbolizationWorksSandboxedLibunwindProcDirMounted) {
  SKIP_SANITIZERS_AND_COVERAGE;
  TemporaryFlagOverride<bool> temp_override(
      &FLAGS_sandbox_libunwind_crash_handler, true);
  SymbolizationWorksCommon(
      [](PolicyBuilder* builder) { builder->AddDirectory("/proc"); });
}

TEST(StackTraceTest, SymbolizationWorksSandboxedLibunwindProcFileMounted) {
  SKIP_SANITIZERS_AND_COVERAGE;
  TemporaryFlagOverride<bool> temp_override(
      &FLAGS_sandbox_libunwind_crash_handler, true);
  SymbolizationWorksCommon([](PolicyBuilder* builder) {
    builder->AddFile("/proc/sys/vm/overcommit_memory");
  });
}

TEST(StackTraceTest, SymbolizationWorksSandboxedLibunwindSysDirMounted) {
  SKIP_SANITIZERS_AND_COVERAGE;
  TemporaryFlagOverride<bool> temp_override(
      &FLAGS_sandbox_libunwind_crash_handler, true);
  SymbolizationWorksCommon(
      [](PolicyBuilder* builder) { builder->AddDirectory("/sys"); });
}

TEST(StackTraceTest, SymbolizationWorksSandboxedLibunwindSysFileMounted) {
  SKIP_SANITIZERS_AND_COVERAGE;
  TemporaryFlagOverride<bool> temp_override(
      &FLAGS_sandbox_libunwind_crash_handler, true);
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
  // Get list of open FDs in the global forkserver.
  pid_t forkserver_pid = GetGlobalForkServerPid();
  std::string forkserver_fd_path =
      absl::StrCat("/proc/", forkserver_pid, "/fd");
  size_t filecount_before = FileCountInDirectory(forkserver_fd_path);

  TemporaryFlagOverride<bool> temp_override(
      &FLAGS_sandbox_libunwind_crash_handler, true);
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
  auto executor = absl::make_unique<Executor>(path, args);
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, PolicyBuilder{}
                                        // Don't restrict the syscalls at all.
                                        .DangerDefaultAllowAll()
                                        .EnableNamespaces()
                                        .AddFile(path)
                                        .AddLibrariesForBinary(path)
                                        .TryBuild());

  Sandbox2 s2(std::move(executor), std::move(policy));
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::SIGNALED));
  ASSERT_THAT(result.GetStackTrace(), Not(HasSubstr("CrashMe")));
}

}  // namespace
}  // namespace sandbox2
