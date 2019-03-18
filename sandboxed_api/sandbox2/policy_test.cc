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

#include "sandboxed_api/sandbox2/policy.h"

#include <sys/resource.h>
#include <syscall.h>

#include <cerrno>
#include <cstdlib>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/limits.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/sandbox2/testing.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"

using ::testing::Eq;

namespace sandbox2 {
namespace {

std::unique_ptr<Policy> PolicyTestcasePolicy() {
  return PolicyBuilder()
      .AllowStaticStartup()
      .AllowExit()
      .AllowRead()
      .AllowWrite()
      .AllowSyscall(__NR_close)
      .AllowSyscall(__NR_getppid)
      .BlockSyscallWithErrno(__NR_open, ENOENT)
      .BlockSyscallWithErrno(__NR_openat, ENOENT)
      .BlockSyscallWithErrno(__NR_access, ENOENT)
      .BlockSyscallWithErrno(__NR_prlimit64, EPERM)
      .BuildOrDie();
}

#if defined(__x86_64__)
// Test that 32-bit syscalls from 64-bit are disallowed.
TEST(PolicyTest, AMD64Syscall32PolicyAllowed) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path = GetTestSourcePath("sandbox2/testcases/policy");

  std::vector<std::string> args = {path, "1"};
  auto executor = absl::make_unique<Executor>(path, args);

  auto policy = PolicyTestcasePolicy();

  Sandbox2 s2(std::move(executor), std::move(policy));
  auto result = s2.Run();
    ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
    EXPECT_THAT(result.reason_code(), Eq(1));  // __NR_exit in 32-bit
    EXPECT_THAT(result.GetSyscallArch(), Eq(Syscall::kX86_32));
}

// Test that 32-bit syscalls from 64-bit for FS checks are disallowed.
TEST(PolicyTest, AMD64Syscall32FsAllowed) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path = GetTestSourcePath("sandbox2/testcases/policy");
  std::vector<std::string> args = {path, "2"};
  auto executor = absl::make_unique<Executor>(path, args);

  auto policy = PolicyTestcasePolicy();

  Sandbox2 s2(std::move(executor), std::move(policy));
  auto result = s2.Run();
    ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
    EXPECT_THAT(result.reason_code(),
                Eq(33));  // __NR_access in 32-bit
    EXPECT_THAT(result.GetSyscallArch(), Eq(Syscall::kX86_32));
}
#endif  // defined(__x86_64__)

// Test that ptrace(2) is disallowed.
TEST(PolicyTest, PtraceDisallowed) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path = GetTestSourcePath("sandbox2/testcases/policy");
  std::vector<std::string> args = {path, "3"};
  auto executor = absl::make_unique<Executor>(path, args);

  auto policy = PolicyTestcasePolicy();

  Sandbox2 s2(std::move(executor), std::move(policy));
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(__NR_ptrace));
}

// Test that clone(2) with flag CLONE_UNTRACED is disallowed.
TEST(PolicyTest, CloneUntracedDisallowed) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path = GetTestSourcePath("sandbox2/testcases/policy");
  std::vector<std::string> args = {path, "4"};
  auto executor = absl::make_unique<Executor>(path, args);

  auto policy = PolicyTestcasePolicy();

  Sandbox2 s2(std::move(executor), std::move(policy));
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(__NR_clone));
}

// Test that bpf(2) is disallowed.
TEST(PolicyTest, BpfDisallowed) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path = GetTestSourcePath("sandbox2/testcases/policy");
  std::vector<std::string> args = {path, "5"};
  auto executor = absl::make_unique<Executor>(path, args);

  auto policy = PolicyTestcasePolicy();

  Sandbox2 s2(std::move(executor), std::move(policy));
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(__NR_bpf));
}

std::unique_ptr<Policy> MinimalTestcasePolicy() {
  return PolicyBuilder()
      .AllowStaticStartup()
      .AllowExit()
      .BlockSyscallWithErrno(__NR_prlimit64, EPERM)
      .BlockSyscallWithErrno(__NR_access, ENOENT)
      .EnableNamespaces()
      .BuildOrDie();
}

// Test that we can sandbox a minimal static binary returning 0.
// If this starts failing, it means something changed, maybe in the way we
// compile static binaries, and we need to update the policy just above.
TEST(MinimalTest, MinimalBinaryWorks) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path = GetTestSourcePath("sandbox2/testcases/minimal");
  std::vector<std::string> args = {path};
  auto executor = absl::make_unique<Executor>(path, args);

  auto policy = MinimalTestcasePolicy();

  Sandbox2 s2(std::move(executor), std::move(policy));
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(EXIT_SUCCESS));
}

// Test that we can sandbox a minimal non-static binary returning 0.
TEST(MinimalTest, MinimalSharedBinaryWorks) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path = GetTestSourcePath("sandbox2/testcases/minimal_dynamic");
  std::vector<std::string> args = {path};
  auto executor = absl::make_unique<Executor>(path, args);

  auto policy = PolicyBuilder()
                    .AllowDynamicStartup()
                    .AllowOpen()
                    .AllowExit()
                    .AllowMmap()
                    // New glibc accesses /etc/ld.so.preload
                    .BlockSyscallWithErrno(__NR_access, ENOENT)
                    .BlockSyscallWithErrno(__NR_prlimit64, EPERM)
                    .EnableNamespaces()
                    .AddLibrariesForBinary(path)
                    .BuildOrDie();

  Sandbox2 s2(std::move(executor), std::move(policy));
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(EXIT_SUCCESS));
}

// Test that the AllowSystemMalloc helper works as expected.
TEST(MallocTest, SystemMallocWorks) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path = GetTestSourcePath("sandbox2/testcases/malloc_system");
  std::vector<std::string> args = {path};
  auto executor = absl::make_unique<Executor>(path, args);

  auto policy = PolicyBuilder()
                    .AllowStaticStartup()
                    .AllowSystemMalloc()
                    .AllowExit()
                    .EnableNamespaces()
                    .BlockSyscallWithErrno(__NR_prlimit64, EPERM)
                    .BlockSyscallWithErrno(__NR_access, ENOENT)
                    .BuildOrDie();

  Sandbox2 s2(std::move(executor), std::move(policy));
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
  auto executor = absl::make_unique<Executor>(path, args);

  auto policy =
      PolicyBuilder()
          .BlockSyscallWithErrno(__NR_open, ENOENT)
          .BlockSyscallWithErrno(__NR_openat, ENOENT)
          .AllowStaticStartup()
          .AllowTcMalloc()
          .AllowExit()
          .AddPolicyOnSyscalls(
              {__NR_getuid, __NR_getgid, __NR_geteuid, __NR_getegid}, {ALLOW})
          .AddPolicyOnSyscalls({__NR_getresuid, __NR_getresgid}, {ERRNO(42)})
          .AddPolicyOnSyscalls({__NR_read, __NR_write}, {ERRNO(43)})
          .AddPolicyOnSyscall(__NR_umask, {DENY})
          .EnableNamespaces()
          .BlockSyscallWithErrno(__NR_prlimit64, EPERM)
          .BlockSyscallWithErrno(__NR_access, ENOENT)
          .BuildOrDie();

  Sandbox2 s2(std::move(executor), std::move(policy));
  auto result = s2.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(__NR_umask));
}

}  // namespace
}  // namespace sandbox2
