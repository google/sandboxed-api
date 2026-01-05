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
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/allowlists/map_exec.h"
#include "sandboxed_api/sandbox2/allowlists/seccomp_speculation.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/path.h"

namespace sandbox2 {
class PolicyBuilderPeer {
 public:
  static PolicyBuilder& OverridableBlockSyscallWithErrno(PolicyBuilder& builder,
                                                         uint32_t num,
                                                         int error) {
    return builder.OverridableBlockSyscallWithErrno(num, error);
  }

  static PolicyBuilder& OverridableAddPolicyOnSyscalls(
      PolicyBuilder& builder, absl::Span<const uint32_t> nums,
      absl::Span<const sock_filter> policy) {
    return builder.OverridableAddPolicyOnSyscalls(nums, policy);
  }
};

namespace {

using ::absl_testing::IsOk;
using ::sapi::CreateDefaultPermissiveTestPolicy;
using ::sapi::GetTestSourcePath;
using ::testing::Eq;

std::string GetBinaryFromArgs(const std::vector<std::string>& args) {
  return !absl::StrContains(args[0], "/")
             ? GetTestSourcePath(
                   sapi::file::JoinPath("sandbox2/testcases", args[0]))
             : args[0];
}

class PolicyTest : public ::testing::TestWithParam<bool> {
 public:
  std::unique_ptr<Sandbox2> CreateTestSandbox(
      const std::vector<std::string>& args, PolicyBuilder builder,
      bool sandbox_pre_execve = true) {
    CHECK(!args.empty());
    if (GetParam()) {
      builder.CollectStacktracesOnSignal(false);
    }
    auto executor = std::make_unique<Executor>(GetBinaryFromArgs(args), args);
    executor->set_enable_sandbox_before_exec(sandbox_pre_execve);
    auto sandbox =
        std::make_unique<Sandbox2>(std::move(executor), builder.BuildOrDie());
    if (GetParam()) {
      CHECK_OK(sandbox->EnableUnotifyMonitor());
    }
    return sandbox;
  }

  std::unique_ptr<Sandbox2> CreatePermissiveTestSandbox(
      std::vector<std::string> args, bool sandbox_pre_execve = true) {
    return CreateTestSandbox(
        args, CreateDefaultPermissiveTestPolicy(GetBinaryFromArgs(args)),
        sandbox_pre_execve);
  }
};

#ifdef SAPI_X86_64

// Test that 32-bit syscalls from 64-bit are disallowed.
TEST_P(PolicyTest, AMD64Syscall32PolicyAllowed) {
  Result result = CreatePermissiveTestSandbox({"policy", "1"})->Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(1));  // __NR_exit in 32-bit
  EXPECT_THAT(result.GetSyscallArch(), Eq(sapi::cpu::kX86));
}

// Test that 32-bit syscalls from 64-bit for FS checks are disallowed.
TEST_P(PolicyTest, AMD64Syscall32FsAllowed) {
  Result result = CreatePermissiveTestSandbox({"policy", "2"})->Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(),
              Eq(33));  // __NR_access in 32-bit
  EXPECT_THAT(result.GetSyscallArch(), Eq(sapi::cpu::kX86));
}

#endif  // SAPI_X86_64

// Test that ptrace(2) is disallowed.
TEST_P(PolicyTest, PtraceDisallowed) {
  Result result = CreatePermissiveTestSandbox({"policy", "3"})->Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(__NR_ptrace));
}

// Test that clone(2) with flag CLONE_UNTRACED is disallowed with PtraceMonitor.
TEST_P(PolicyTest, CloneUntrace) {
  Result result = CreatePermissiveTestSandbox({"policy", "4"})->Run();

  if (GetParam()) {
    ASSERT_THAT(result.final_status(), Eq(Result::OK));
    EXPECT_THAT(result.reason_code(), Eq(EXIT_FAILURE));
  } else {
    ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
    EXPECT_THAT(result.reason_code(), Eq(__NR_clone));
  }
}

// Test that bpf(2) is disallowed.
TEST_P(PolicyTest, BpfDisallowed) {
  Result result = CreatePermissiveTestSandbox({"policy", "5"})->Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(__NR_bpf));
}

// Test that ptrace/bpf can return EPERM.
TEST_P(PolicyTest, BpfPtracePermissionDenied) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/policy");
  std::unique_ptr<Sandbox2> s2 = CreateTestSandbox(
      {"policy", "7"},
      CreateDefaultPermissiveTestPolicy(path).BlockSyscallsWithErrno(
          {__NR_ptrace, __NR_bpf}, EPERM));
  Result result = s2->Run();

  // ptrace/bpf is not a violation due to explicit policy. EPERM is expected.
  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));
}

// Test that we can allow safe uses of bpf().
TEST_P(PolicyTest, BpfAllowSafe) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/policy");
  {
    std::unique_ptr<Sandbox2> s2 = CreateTestSandbox(
        {"policy", "9"},  // Calls TestSafeBpf()
        CreateDefaultPermissiveTestPolicy(path).AllowSafeBpf());
    Result result = s2->Run();

    ASSERT_THAT(result.final_status(), Eq(Result::OK));
    EXPECT_THAT(result.reason_code(), Eq(0));
  }
  {
    std::unique_ptr<Sandbox2> s2 = CreateTestSandbox(
        {"policy", "5"},  // Calls TestBpf()
        CreateDefaultPermissiveTestPolicy(path).AllowSafeBpf());
    Result result = s2->Run();

    ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
    EXPECT_THAT(result.reason_code(), Eq(__NR_bpf));
  }
}

// Test that bpf can return EPERM even after AllowSafeBpf() is called.
TEST_P(PolicyTest, BpfAllowSafeButBlock) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/policy");
  {
    std::unique_ptr<Sandbox2> s2 =
        CreateTestSandbox({"policy", "8"},  // Calls TestBpfBlocked()
                          CreateDefaultPermissiveTestPolicy(path)
                              .AllowSafeBpf()
                              .BlockSyscallWithErrno(__NR_bpf, EPERM));
    Result result = s2->Run();

    ASSERT_THAT(result.final_status(), Eq(Result::OK));
    EXPECT_THAT(result.reason_code(), Eq(0));
  }
  {
    std::unique_ptr<Sandbox2> s2 =
        CreateTestSandbox({"policy", "9"},  // Calls TestSafeBpf()
                          CreateDefaultPermissiveTestPolicy(path)
                              .AllowSafeBpf()
                              .BlockSyscallWithErrno(__NR_bpf, EPERM));
    Result result = s2->Run();

    ASSERT_THAT(result.final_status(), Eq(Result::OK));
    EXPECT_THAT(result.reason_code(), Eq(0));
  }
}

TEST_P(PolicyTest, IsattyAllowed) {
  SKIP_SANITIZERS;
  std::unique_ptr<Sandbox2> s2 =
      CreateTestSandbox({"policy", "6"}, PolicyBuilder()
                                             .AllowStaticStartup()
                                             .AllowExit()
                                             .AllowRead()
                                             .AllowWrite()
                                             .AllowTCGETS()
                                             .AllowLlvmCoverage());
  Result result = s2->Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
}

PolicyBuilder PosixTimersPolicyBuilder() {
  return PolicyBuilder()
      // Required by google infra / logging.
      .AllowDynamicStartup(sandbox2::MapExec())
      .AllowWrite()
      .AllowSyscall(__NR_getcwd)
      .AllowMmapWithoutExec()
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

TEST_P(PolicyTest, PosixTimersWorkIfAllowed) {
  SKIP_SANITIZERS;
  for (absl::string_view kind : {"SIGEV_NONE", "SIGEV_SIGNAL",
                                 "SIGEV_THREAD_ID", "syscall(SIGEV_THREAD)"}) {
    std::unique_ptr<Sandbox2> s2 = CreateTestSandbox(
        {"posix_timers", "--sigev_notify_kind", std::string(kind)},
        PosixTimersPolicyBuilder());
    Result result = s2->Run();
    EXPECT_EQ(result.final_status(), Result::OK) << kind;
  }
}

TEST_P(PolicyTest, PosixTimersCannotCreateThreadsIfThreadsAreProhibited) {
  SKIP_SANITIZERS;
  std::unique_ptr<Sandbox2> s2 = CreateTestSandbox(
      {"posix_timers",
       // SIGEV_THREAD creates a thread as an implementation detail.
       "--sigev_notify_kind=SIGEV_THREAD"},
      PosixTimersPolicyBuilder());
  Result result = s2->Run();
  EXPECT_EQ(result.final_status(), Result::VIOLATION);
}

TEST_P(PolicyTest, PosixTimersCanCreateThreadsIfThreadsAreAllowed) {
  SKIP_SANITIZERS;
  std::unique_ptr<Sandbox2> s2 =
      CreateTestSandbox({"posix_timers", "--sigev_notify_kind=SIGEV_THREAD"},
                        PosixTimersPolicyBuilder()
                            .AllowFork()
                            // For Arm.
                            .AllowSyscall(__NR_madvise));
  Result result = s2->Run();
  EXPECT_EQ(result.final_status(), Result::OK);
}

PolicyBuilder MinimalTestcasePolicyBuilder() {
  return PolicyBuilder().AllowStaticStartup().AllowExit().AllowLlvmCoverage();
}

// Test that we can sandbox a minimal static binary returning 0.
// If this starts failing, it means something changed, maybe in the way we
// compile static binaries, and we need to update the policy just above.
TEST_P(PolicyTest, MinimalBinaryWorks) {
  SKIP_SANITIZERS;
  std::unique_ptr<Sandbox2> s2 =
      CreateTestSandbox({"minimal"}, MinimalTestcasePolicyBuilder());
  Result result = s2->Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(EXIT_SUCCESS));
}

// Test that we can sandbox a minimal non-static binary returning 0.
TEST_P(PolicyTest, MinimalSharedBinaryWorks) {
  SKIP_SANITIZERS;
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/minimal_dynamic");
  std::unique_ptr<Sandbox2> s2 =
      CreateTestSandbox({path}, PolicyBuilder()
                                    .AddLibrariesForBinary(path)
                                    .AllowDynamicStartup(sandbox2::MapExec())
                                    .AllowExit()
                                    .AllowLlvmCoverage());
  Result result = s2->Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(EXIT_SUCCESS));
}

// Test that the AllowSystemMalloc helper works as expected.
TEST_P(PolicyTest, SystemMallocWorks) {
  SKIP_SANITIZERS;
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/malloc_system");
  std::unique_ptr<Sandbox2> s2 =
      CreateTestSandbox({path}, PolicyBuilder()
                                    .AllowStaticStartup()
                                    .AllowSystemMalloc()
                                    .AllowExit()
                                    .AllowLlvmCoverage());
  Result result = s2->Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(EXIT_SUCCESS));
}

// Complicated test to see that AddPolicyOnSyscalls work as
// expected. Specifically a worrisome corner-case would be that the logic was
// almost correct, but that the jump targets were off slightly. This uses the
// AddPolicyOnSyscall multiple times in a row to make any miscalculation
// unlikely to pass this check.
TEST_P(PolicyTest, AddPolicyOnSyscallsWorks) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/add_policy_on_syscalls");
  std::unique_ptr<Sandbox2> s2 = CreateTestSandbox(
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
  Result result = s2->Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(__NR_umask));
}

// Test that util::kMagicSyscallNo is returns ENOSYS or util::kMagicSyscallErr.
TEST_P(PolicyTest, DetectSandboxSyscall) {
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/sandbox_detection");
  std::unique_ptr<Sandbox2> s2 =
      CreatePermissiveTestSandbox({path}, /*sandbox_pre_execve=*/false);
  Result result = s2->Run();

  // The test binary should exit with success.
  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));
}

TEST_P(PolicyTest, ExecveatNotAllowedByDefault) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/execveat");

  std::unique_ptr<Sandbox2> s2 = CreateTestSandbox(
      {path, "1"},
      CreateDefaultPermissiveTestPolicy(path).BlockSyscallWithErrno(
          __NR_execveat, EPERM),
      /*sandbox_pre_execve=*/false);
  Result result = s2->Run();

  // The test binary should exit with success.
  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));
}

TEST_P(PolicyTest, SecondExecveatNotAllowedByDefault) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/execveat");

  std::unique_ptr<Sandbox2> s2 = CreateTestSandbox(
      {path, "2"},
      CreateDefaultPermissiveTestPolicy(path).BlockSyscallWithErrno(
          __NR_execveat, EPERM));
  Result result = s2->Run();

  // The test binary should exit with success.
  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));
}

// TODO: b/453946404 - Re-enable the next four tests once the bug is fixed.
TEST_P(PolicyTest, DISABLED_MmapWithExecNotAllowedByDefault) {
  SKIP_SANITIZERS_AND_COVERAGE;

  const std::string path = GetTestSourcePath("sandbox2/testcases/mmap");

  std::unique_ptr<Sandbox2> s2 =
      CreateTestSandbox({path, "1"}, CreateDefaultPermissiveTestPolicy(path));
  Result result = s2->Run();

  // The test binary should exit with success.
  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(__NR_mmap));
}

TEST_P(PolicyTest, DISABLED_MmapWithExecAllowed) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/mmap");

  std::unique_ptr<Sandbox2> s2 = CreateTestSandbox(
      {path, "1"}, CreateDefaultPermissiveTestPolicy(path).Allow(MapExec()));
  Result result = s2->Run();

  // The test binary should exit with success.
  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));
}

TEST_P(PolicyTest, DISABLED_MprotectWithExecNotAllowedByDefault) {
  SKIP_SANITIZERS_AND_COVERAGE;

  const std::string path = GetTestSourcePath("sandbox2/testcases/mmap");

  std::unique_ptr<Sandbox2> s2 =
      CreateTestSandbox({path, "2"}, CreateDefaultPermissiveTestPolicy(path));
  Result result = s2->Run();

  // The test binary should exit with success.
  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(__NR_mprotect));
}

TEST_P(PolicyTest, DISABLED_MprotectWithExecAllowed) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/mmap");

  std::unique_ptr<Sandbox2> s2 = CreateTestSandbox(
      {path, "2"}, CreateDefaultPermissiveTestPolicy(path).Allow(MapExec()));
  Result result = s2->Run();

  // The test binary should exit with success.
  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));
}

#ifdef SAPI_X86_64
TEST_P(PolicyTest, SpeculationAllowed) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/policy");
  std::unique_ptr<Sandbox2> s2 = CreateTestSandbox(
      {"policy", "11"},  // Calls TestSpeculationAllowed()
      CreateDefaultPermissiveTestPolicy(path).Allow(SeccompSpeculation()));
  Result result = s2->Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));
}

TEST_P(PolicyTest, SpeculationBlockedByDefault) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/policy");
  std::unique_ptr<Sandbox2> s2 =
      CreateTestSandbox({"policy", "12"},  // Calls TestSpeculationBlocked()
                        CreateDefaultPermissiveTestPolicy(path));
  Result result = s2->Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));
}
#endif  // SAPI_X86_64

PolicyBuilder PolicyTestcasePolicyBuilder() {
  return MinimalTestcasePolicyBuilder().AllowWrite();
}

TEST_P(PolicyTest, OverridableBlockSyscallWithErrnoWorks) {
  SKIP_SANITIZERS;
  PolicyBuilder policy_builder = PolicyTestcasePolicyBuilder();
  PolicyBuilderPeer::OverridableBlockSyscallWithErrno(policy_builder, 1337, 2);
  policy_builder.AddPolicyOnSyscall(1337, {
                                              ARG_32(0),
                                              JEQ32(1, ERRNO(1)),
                                          });
  std::unique_ptr<Sandbox2> s2 =
      CreateTestSandbox({"policy", "13", "1337", "1", "1"}, policy_builder);
  Result result = s2->Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));
  s2 = CreateTestSandbox({"policy", "13", "1337", "2", "2"}, policy_builder);
  result = s2->Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));
}

TEST_P(PolicyTest, OverridablePolicyOnSyscallsWorks) {
  SKIP_SANITIZERS;
  PolicyBuilder policy_builder = PolicyTestcasePolicyBuilder();
  PolicyBuilderPeer::OverridableAddPolicyOnSyscalls(policy_builder, {1337},
                                                    {
                                                        ARG_32(0),
                                                        JEQ32(1, ERRNO(1)),
                                                        JEQ32(2, ERRNO(3)),
                                                    });
  policy_builder.AddPolicyOnSyscall(1337, {
                                              ARG_32(0),
                                              JEQ32(2, ERRNO(2)),
                                          });
  std::unique_ptr<Sandbox2> s2 =
      CreateTestSandbox({"policy", "13", "1337", "1", "1"}, policy_builder);
  Result result = s2->Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));

  s2 = CreateTestSandbox({"policy", "13", "1337", "2", "2"}, policy_builder);
  result = s2->Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));
}

INSTANTIATE_TEST_SUITE_P(Sandbox2, PolicyTest, ::testing::Values(false, true),
                         [](const ::testing::TestParamInfo<bool>& info) {
                           return info.param ? "UnotifyMonitor"
                                             : "PtraceMonitor";
                         });

}  // namespace
}  // namespace sandbox2
