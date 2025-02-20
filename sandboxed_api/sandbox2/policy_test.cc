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
#include "absl/log/check.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sandbox2 {
namespace {

using ::sapi::CreateDefaultPermissiveTestPolicy;
using ::sapi::GetTestSourcePath;
using ::testing::Eq;

std::string GetBinaryFromArgs(const std::vector<std::string>& args) {
  return !absl::StrContains(args[0], "/")
             ? GetTestSourcePath(
                   sapi::file::JoinPath("sandbox2/testcases", args[0]))
             : args[0];
}

Sandbox2 CreateTestSandbox(const std::vector<std::string>& args,
                           PolicyBuilder builder) {
  CHECK(!args.empty());
  return Sandbox2(std::make_unique<Executor>(GetBinaryFromArgs(args), args),
                  builder.BuildOrDie());
}

Sandbox2 CreatePermissiveTestSandbox(std::vector<std::string> args) {
  return CreateTestSandbox(
      args, CreateDefaultPermissiveTestPolicy(GetBinaryFromArgs(args)));
}

#ifdef SAPI_X86_64

// Test that 32-bit syscalls from 64-bit are disallowed.
TEST(PolicyTest, AMD64Syscall32PolicyAllowed) {
  Result result = CreatePermissiveTestSandbox({"policy", "1"}).Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(1));  // __NR_exit in 32-bit
  EXPECT_THAT(result.GetSyscallArch(), Eq(sapi::cpu::kX86));
}

// Test that 32-bit syscalls from 64-bit for FS checks are disallowed.
TEST(PolicyTest, AMD64Syscall32FsAllowed) {
  Result result = CreatePermissiveTestSandbox({"policy", "2"}).Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(),
              Eq(33));  // __NR_access in 32-bit
  EXPECT_THAT(result.GetSyscallArch(), Eq(sapi::cpu::kX86));
}

#endif  // SAPI_X86_64

// Test that ptrace(2) is disallowed.
TEST(PolicyTest, PtraceDisallowed) {
  Result result = CreatePermissiveTestSandbox({"policy", "3"}).Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(__NR_ptrace));
}

// Test that clone(2) with flag CLONE_UNTRACED is disallowed.
TEST(PolicyTest, CloneUntracedDisallowed) {
  Result result = CreatePermissiveTestSandbox({"policy", "4"}).Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(__NR_clone));
}

// Test that bpf(2) is disallowed.
TEST(PolicyTest, BpfDisallowed) {
  Result result = CreatePermissiveTestSandbox({"policy", "5"}).Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(__NR_bpf));
}

// Test that ptrace/bpf can return EPERM.
TEST(PolicyTest, BpfPtracePermissionDenied) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/policy");
  Sandbox2 s2 = CreateTestSandbox(
      {"policy", "7"},
      CreateDefaultPermissiveTestPolicy(path).BlockSyscallsWithErrno(
          {__NR_ptrace, __NR_bpf}, EPERM));
  Result result = s2.Run();

  // ptrace/bpf is not a violation due to explicit policy.  EPERM is expected.
  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));
}

TEST(PolicyTest, IsattyAllowed) {
  SKIP_SANITIZERS;
  Sandbox2 s2 = CreateTestSandbox({"policy", "6"}, PolicyBuilder()
                                                       .AllowStaticStartup()
                                                       .AllowExit()
                                                       .AllowRead()
                                                       .AllowWrite()
                                                       .AllowTCGETS()
                                                       .AllowLlvmCoverage());
  Result result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
}

PolicyBuilder PosixTimersPolicyBuilder() {
  return PolicyBuilder()
      // Required by google infra / logging.
      .AllowDynamicStartup()
      .AllowWrite()
      .AllowSyscall(__NR_getcwd)
      .AllowMmap()
      .AllowMlock()
      .AllowMkdir()
      .AllowGetIDs()
      .AllowExit()
      .AllowRestartableSequences(PolicyBuilder::kAllowSlowFences)
      .AllowSyscall(__NR_rt_sigtimedwait)
      // Features used by the binary.
      .AllowHandleSignals()
      .AllowGetPIDs()
      .AllowTime()
      .AllowSleep()
      .AllowAlarm()
      // Posix timers themselves.
      .AllowPosixTimers();
}

TEST(PolicyTest, PosixTimersWorkIfAllowed) {
  SKIP_SANITIZERS;
  for (absl::string_view kind : {"SIGEV_NONE", "SIGEV_SIGNAL",
                                 "SIGEV_THREAD_ID", "syscall(SIGEV_THREAD)"}) {
    Sandbox2 s2 = CreateTestSandbox(
        {"posix_timers", "--sigev_notify_kind", std::string(kind)},
        PosixTimersPolicyBuilder());
    Result result = s2.Run();
    EXPECT_EQ(result.final_status(), Result::OK) << kind;
  }
}

TEST(PolicyTest, PosixTimersCannotCreateThreadsIfThreadsAreProhibited) {
  SKIP_SANITIZERS;
  Sandbox2 s2 = CreateTestSandbox(
      {"posix_timers",
       // SIGEV_THREAD creates a thread as an implementation detail.
       "--sigev_notify_kind=SIGEV_THREAD"},
      PosixTimersPolicyBuilder());
  Result result = s2.Run();
  EXPECT_EQ(result.final_status(), Result::VIOLATION);
}

TEST(PolicyTest, PosixTimersCanCreateThreadsIfThreadsAreAllowed) {
  SKIP_SANITIZERS;
  Sandbox2 s2 =
      CreateTestSandbox({"posix_timers", "--sigev_notify_kind=SIGEV_THREAD"},
                        PosixTimersPolicyBuilder()
                            .AllowFork()
                            // For Arm.
                            .AllowSyscall(__NR_madvise));
  Result result = s2.Run();
  EXPECT_EQ(result.final_status(), Result::OK);
}

PolicyBuilder MinimalTestcasePolicyBuilder() {
  return PolicyBuilder().AllowStaticStartup().AllowExit().AllowLlvmCoverage();
}

// Test that we can sandbox a minimal static binary returning 0.
// If this starts failing, it means something changed, maybe in the way we
// compile static binaries, and we need to update the policy just above.
TEST(MinimalTest, MinimalBinaryWorks) {
  SKIP_SANITIZERS;
  Sandbox2 s2 = CreateTestSandbox({"minimal"}, MinimalTestcasePolicyBuilder());
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(EXIT_SUCCESS));
}

// Test that we can sandbox a minimal non-static binary returning 0.
TEST(MinimalTest, MinimalSharedBinaryWorks) {
  SKIP_SANITIZERS;
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/minimal_dynamic");
  Sandbox2 s2 = CreateTestSandbox({path}, PolicyBuilder()
                                              .AddLibrariesForBinary(path)
                                              .AllowDynamicStartup()
                                              .AllowExit()
                                              .AllowLlvmCoverage());
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(EXIT_SUCCESS));
}

// Test that the AllowSystemMalloc helper works as expected.
TEST(MallocTest, SystemMallocWorks) {
  SKIP_SANITIZERS;
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/malloc_system");
  Sandbox2 s2 = CreateTestSandbox({path}, PolicyBuilder()
                                              .AllowStaticStartup()
                                              .AllowSystemMalloc()
                                              .AllowExit()
                                              .AllowLlvmCoverage());
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
  Sandbox2 s2 = CreateTestSandbox(
      {path}, PolicyBuilder()
                  .AllowStaticStartup()
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
                  .AddPolicyOnSyscalls({__NR_write}, {ERRNO(43)})
                  .AddPolicyOnSyscall(__NR_umask, {DENY}));
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(__NR_umask));
}

// Test that util::kMagicSyscallNo is returns ENOSYS or util::kMagicSyscallErr.
TEST(PolicyTest, DetectSandboxSyscall) {
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/sandbox_detection");
  const std::vector<std::string> args = {path};

  auto executor = std::make_unique<Executor>(path, args);
  executor->set_enable_sandbox_before_exec(false);
  SAPI_ASSERT_OK_AND_ASSIGN(std::unique_ptr<Policy> policy,
                            CreateDefaultPermissiveTestPolicy(path).TryBuild());
  Sandbox2 s2(std::move(executor), std::move(policy));
  auto result = s2.Run();

  // The test binary should exit with success.
  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));
}

}  // namespace
}  // namespace sandbox2
