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

#include "sandboxed_api/sandbox2/policy.h"

#include <syscall.h>

#include <cerrno>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sandbox2 {
namespace {

using ::sapi::CreateDefaultPermissiveTestPolicy;
using ::sapi::GetTestSourcePath;
using ::testing::Eq;

#ifdef SAPI_X86_64
// Test that 32-bit syscalls from 64-bit are disallowed.
TEST(PolicyTest, AMD64Syscall32PolicyAllowed) {
  SKIP_ANDROID;
  const std::string path = GetTestSourcePath("sandbox2/testcases/policy");

  std::vector<std::string> args = {path, "1"};

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            CreateDefaultPermissiveTestPolicy(path).TryBuild());
  Sandbox2 s2(std::make_unique<Executor>(path, args), std::move(policy));
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(1));  // __NR_exit in 32-bit
  EXPECT_THAT(result.GetSyscallArch(), Eq(sapi::cpu::kX86));
}

// Test that 32-bit syscalls from 64-bit for FS checks are disallowed.
TEST(PolicyTest, AMD64Syscall32FsAllowed) {
  SKIP_ANDROID;
  const std::string path = GetTestSourcePath("sandbox2/testcases/policy");
  std::vector<std::string> args = {path, "2"};

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            CreateDefaultPermissiveTestPolicy(path).TryBuild());
  Sandbox2 s2(std::make_unique<Executor>(path, args), std::move(policy));
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(),
              Eq(33));  // __NR_access in 32-bit
  EXPECT_THAT(result.GetSyscallArch(), Eq(sapi::cpu::kX86));
}
#endif

// Test that ptrace(2) is disallowed.
TEST(PolicyTest, PtraceDisallowed) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/policy");
  std::vector<std::string> args = {path, "3"};

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            CreateDefaultPermissiveTestPolicy(path).TryBuild());
  Sandbox2 s2(std::make_unique<Executor>(path, args), std::move(policy));
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(__NR_ptrace));
}

// Test that clone(2) with flag CLONE_UNTRACED is disallowed.
TEST(PolicyTest, CloneUntracedDisallowed) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/policy");
  std::vector<std::string> args = {path, "4"};
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            CreateDefaultPermissiveTestPolicy(path).TryBuild());
  Sandbox2 s2(std::make_unique<Executor>(path, args), std::move(policy));
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(__NR_clone));
}

// Test that bpf(2) is disallowed.
TEST(PolicyTest, BpfDisallowed) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/policy");
  std::vector<std::string> args = {path, "5"};
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            CreateDefaultPermissiveTestPolicy(path).TryBuild());
  Sandbox2 s2(std::make_unique<Executor>(path, args), std::move(policy));
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(__NR_bpf));
}

// Test that ptrace/bpf can return EPERM.
TEST(PolicyTest, BpfPtracePermissionDenied) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/policy");
  std::vector<std::string> args = {path, "7"};

  SAPI_ASSERT_OK_AND_ASSIGN(
      auto policy, CreateDefaultPermissiveTestPolicy(path)
                       .BlockSyscallsWithErrno({__NR_ptrace, __NR_bpf}, EPERM)
                       .TryBuild());
  Sandbox2 s2(std::make_unique<Executor>(path, args), std::move(policy));
  auto result = s2.Run();

  // ptrace/bpf is not a violation due to explicit policy.  EPERM is expected.
  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));
}

TEST(PolicyTest, IsattyAllowed) {
  SKIP_SANITIZERS_AND_COVERAGE;
  sandbox2::PolicyBuilder builder;
  if constexpr (sapi::host_os::IsAndroid()) {
    builder.DisableNamespaces().AllowDynamicStartup();
  }
  builder.AllowStaticStartup()
      .AllowExit()
      .AllowRead()
      .AllowWrite()
      .AllowTCGETS();
  const std::string path = GetTestSourcePath("sandbox2/testcases/policy");
  std::vector<std::string> args = {path, "6"};
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, builder.TryBuild());
  Sandbox2 s2(std::make_unique<Executor>(path, args), std::move(policy));
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
}

std::unique_ptr<Policy> MinimalTestcasePolicy() {
  sandbox2::PolicyBuilder builder;

  if constexpr (sapi::host_os::IsAndroid()) {
    builder.AllowDynamicStartup();
    builder.DisableNamespaces();
  }

  builder.AllowStaticStartup().AllowExit();
  return builder.BuildOrDie();
}

// Test that we can sandbox a minimal static binary returning 0.
// If this starts failing, it means something changed, maybe in the way we
// compile static binaries, and we need to update the policy just above.
TEST(MinimalTest, MinimalBinaryWorks) {
  SKIP_ANDROID;
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path = GetTestSourcePath("sandbox2/testcases/minimal");
  std::vector<std::string> args = {path};
  Sandbox2 s2(std::make_unique<Executor>(path, args), MinimalTestcasePolicy());
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(EXIT_SUCCESS));
}

// Test that we can sandbox a minimal non-static binary returning 0.
TEST(MinimalTest, MinimalSharedBinaryWorks) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/minimal_dynamic");
  std::vector<std::string> args = {path};

  sandbox2::PolicyBuilder builder;

  if constexpr (sapi::host_os::IsAndroid()) {
    builder.DisableNamespaces();
  } else {
    builder.AddLibrariesForBinary(path);
  }

  builder.AllowDynamicStartup().AllowExit();
  auto policy = builder.BuildOrDie();

  Sandbox2 s2(std::make_unique<Executor>(path, args), std::move(policy));
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(EXIT_SUCCESS));
}

// Test that the AllowSystemMalloc helper works as expected.
TEST(MallocTest, SystemMallocWorks) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/malloc_system");
  std::vector<std::string> args = {path};

  sandbox2::PolicyBuilder builder;

  if constexpr (sapi::host_os::IsAndroid()) {
    builder.DisableNamespaces();
    builder.AllowDynamicStartup();
    builder.AllowSyscalls({
        __NR_madvise,
    });
  }

  builder.AllowStaticStartup().AllowSystemMalloc().AllowExit();
  auto policy = builder.BuildOrDie();

  Sandbox2 s2(std::make_unique<Executor>(path, args), std::move(policy));
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(EXIT_SUCCESS));
}

// Complicated test to see that AddPolicyOnSyscalls work as
// expected. Specifically a worrisome corner-case would be that the logic was
// almost correct, but that the jump targets were off slightly. This uses the
// AddPolicyOnSyscall multiple times in a row to make any miscalculation
// unlikely to pass this check.
TEST(MultipleSyscalls, AddPolicyOnSyscallsWorks) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/add_policy_on_syscalls");
  std::vector<std::string> args = {path};

  sandbox2::PolicyBuilder builder;
  if constexpr (sapi::host_os::IsAndroid()) {
    builder.DisableNamespaces();
    builder.AllowDynamicStartup();
  }

  builder.AllowStaticStartup()
      .AllowTcMalloc()
      .AllowExit()
      .AddPolicyOnSyscalls(
          {
              __NR_getuid,
              __NR_getgid,
              __NR_geteuid,
              __NR_getegid,
#ifdef __NR_getuid32
              __NR_getuid32,
#endif
#ifdef __NR_getgid32
              __NR_getgid32,
#endif
#ifdef __NR_geteuid32
              __NR_geteuid32,
#endif
#ifdef __NR_getegid32
              __NR_getegid32,
#endif
          },
          {ALLOW})
      .AddPolicyOnSyscalls(
          {
              __NR_getresuid,
              __NR_getresgid,
#ifdef __NR_getresuid32
              __NR_getresuid32,
#endif
#ifdef __NR_getresgid32
              __NR_getresgid32,
#endif
          },
          {ERRNO(42)})
      .AddPolicyOnSyscalls({__NR_read, __NR_write}, {ERRNO(43)})
      .AddPolicyOnSyscall(__NR_umask, {DENY});
  auto policy = builder.BuildOrDie();

  Sandbox2 s2(std::make_unique<Executor>(path, args), std::move(policy));
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(__NR_umask));
}

}  // namespace
}  // namespace sandbox2
