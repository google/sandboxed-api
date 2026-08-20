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

#include "sandboxed_api/sandbox2/result.h"

#include <sys/resource.h>
#include <syscall.h>

#include <csignal>
#include <memory>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "sandboxed_api/sandbox2/syscall.h"

namespace sandbox2 {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::AllOf;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::NotNull;

TEST(ResultTest, StatusEnumToString) {
  EXPECT_THAT(Result::StatusEnumToString(Result::UNSET), Eq("UNSET"));
  EXPECT_THAT(Result::StatusEnumToString(Result::OK), Eq("OK"));
  EXPECT_THAT(Result::StatusEnumToString(Result::SETUP_ERROR),
              Eq("SETUP_ERROR"));
  EXPECT_THAT(Result::StatusEnumToString(Result::VIOLATION), Eq("VIOLATION"));
  EXPECT_THAT(Result::StatusEnumToString(Result::SIGNALED), Eq("SIGNALED"));
  EXPECT_THAT(Result::StatusEnumToString(Result::TIMEOUT), Eq("TIMEOUT"));
  EXPECT_THAT(Result::StatusEnumToString(Result::EXTERNAL_KILL),
              Eq("EXTERNAL_KILL"));
  EXPECT_THAT(Result::StatusEnumToString(Result::INTERNAL_ERROR),
              Eq("INTERNAL_ERROR"));
  EXPECT_THAT(Result::StatusEnumToString(static_cast<Result::StatusEnum>(999)),
              Eq("UNKNOWN"));
}

TEST(ResultTest, ReasonCodeEnumToString) {
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::UNSUPPORTED_ARCH),
              Eq("UNSUPPORTED_ARCH"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_TIMERS),
              Eq("FAILED_TIMERS"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_SIGNALS),
              Eq("FAILED_SIGNALS"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_SUBPROCESS),
              Eq("FAILED_SUBPROCESS"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_NOTIFY),
              Eq("FAILED_NOTIFY"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_CONNECTION),
              Eq("FAILED_CONNECTION"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_WAIT),
              Eq("FAILED_WAIT"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_NAMESPACES),
              Eq("FAILED_NAMESPACES"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_PTRACE),
              Eq("FAILED_PTRACE"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_IPC),
              Eq("FAILED_IPC"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_COMMS_UPGRADE),
              Eq("FAILED_COMMS_UPGRADE"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_LIMITS),
              Eq("FAILED_LIMITS"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_CWD),
              Eq("FAILED_CWD"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_POLICY),
              Eq("FAILED_POLICY"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_VERSION_CHECK),
              Eq("FAILED_VERSION_CHECK"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_CONFIG),
              Eq("FAILED_CONFIG"));

  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_STORE),
              Eq("FAILED_STORE"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_FETCH),
              Eq("FAILED_FETCH"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_GETEVENT),
              Eq("FAILED_GETEVENT"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_MONITOR),
              Eq("FAILED_MONITOR"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_KILL),
              Eq("FAILED_KILL"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_INTERRUPT),
              Eq("FAILED_INTERRUPT"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_CHILD),
              Eq("FAILED_CHILD"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::FAILED_INSPECT),
              Eq("FAILED_INSPECT"));

  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::VIOLATION_SYSCALL),
              Eq("VIOLATION_SYSCALL"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::VIOLATION_ARCH),
              Eq("VIOLATION_ARCH"));
  EXPECT_THAT(Result::ReasonCodeEnumToString(Result::VIOLATION_NETWORK),
              Eq("VIOLATION_NETWORK"));

  EXPECT_THAT(
      Result::ReasonCodeEnumToString(static_cast<Result::ReasonCodeEnum>(999)),
      Eq("UNKNOWN: 999"));
}

TEST(ResultTest, DefaultState) {
  Result result;
  EXPECT_THAT(result.final_status(), Eq(Result::UNSET));
  EXPECT_THAT(result.reason_code(), Eq(0));
  EXPECT_THAT(result.GetRegs(), Eq(nullptr));
  EXPECT_THAT(result.GetSyscall(), Eq(nullptr));
  EXPECT_THAT(result.GetStackTrace(), Eq(""));
  EXPECT_THAT(result.GetProgName(), Eq(""));
  EXPECT_THAT(result.GetProcMaps(), Eq(""));
  EXPECT_THAT(result.ToString(), HasSubstr("UNSET - Code: 0"));
  EXPECT_THAT(result.ToStatus(), StatusIs(absl::StatusCode::kInternal));
}

TEST(ResultTest, StatusOk) {
  Result result;
  result.SetExitStatusCode(Result::OK, 0);
  EXPECT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));
  EXPECT_THAT(result.ToStatus(), IsOk());
  EXPECT_THAT(result.ToString(), HasSubstr("OK - Exit code: 0"));

  result = Result();
  result.SetExitStatusCode(Result::OK, 42);
  EXPECT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(42));
  EXPECT_THAT(result.ToStatus(), StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(result.ToString(), HasSubstr("OK - Exit code: 42"));
}

TEST(ResultTest, StatusTimeout) {
  Result result;
  result.SetExitStatusCode(Result::TIMEOUT, 10);
  EXPECT_THAT(result.final_status(), Eq(Result::TIMEOUT));
  EXPECT_THAT(result.reason_code(), Eq(10));
  EXPECT_THAT(result.ToStatus(), StatusIs(absl::StatusCode::kDeadlineExceeded));
  EXPECT_THAT(result.ToString(), HasSubstr("Process TIMEOUT - Code: 10"));
}

TEST(ResultTest, StatusSetupError) {
  Result result;
  result.SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_NAMESPACES);
  EXPECT_THAT(result.final_status(), Eq(Result::SETUP_ERROR));
  EXPECT_THAT(result.reason_code(), Eq(Result::FAILED_NAMESPACES));
  EXPECT_THAT(result.ToStatus(), StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(result.ToString(),
              HasSubstr("SETUP_ERROR - Code: FAILED_NAMESPACES"));
}

TEST(ResultTest, StatusSignaled) {
  Result result;
  result.SetExitStatusCode(Result::SIGNALED, SIGSEGV);
  EXPECT_THAT(result.final_status(), Eq(Result::SIGNALED));
  EXPECT_THAT(result.reason_code(), Eq(SIGSEGV));
  EXPECT_THAT(result.ToStatus(), StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(result.ToString(),
              AllOf(HasSubstr("Process terminated with a SIGNAL"),
                    HasSubstr("SIGSEGV")));
}

TEST(ResultTest, StatusExternalKill) {
  Result result;
  result.SetExitStatusCode(Result::EXTERNAL_KILL, 1);
  EXPECT_THAT(result.final_status(), Eq(Result::EXTERNAL_KILL));
  EXPECT_THAT(result.ToStatus(), StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(result.ToString(), HasSubstr("Process killed by user - Code: 1"));
}

TEST(ResultTest, StatusInternalError) {
  Result result;
  result.SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_PTRACE);
  EXPECT_THAT(result.final_status(), Eq(Result::INTERNAL_ERROR));
  EXPECT_THAT(result.ToStatus(), StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(result.ToString(),
              HasSubstr("INTERNAL_ERROR - Code: FAILED_PTRACE"));
}

TEST(ResultTest, StatusViolationWithSyscall) {
  Result result;
  result.SetExitStatusCode(Result::VIOLATION, Result::VIOLATION_SYSCALL);
  Syscall::Args args = {1, 2, 3, 0, 0, 0};
  result.SetSyscall(
      std::make_unique<Syscall>(Syscall::GetHostArch(), __NR_read, args));

  EXPECT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.GetSyscall(), NotNull());
  EXPECT_THAT(result.ToString(),
              AllOf(HasSubstr("SYSCALL VIOLATION - Violating Syscall"),
                    HasSubstr("read")));
}

TEST(ResultTest, StatusViolationNetwork) {
  Result result;
  result.SetExitStatusCode(Result::VIOLATION, Result::VIOLATION_NETWORK);
  result.SetNetworkViolation("connect to 127.0.0.1:80 blocked");

  EXPECT_THAT(result.GetNetworkViolation(),
              Eq("connect to 127.0.0.1:80 blocked"));
  EXPECT_THAT(result.ToString(),
              HasSubstr("NETWORK VIOLATION: connect to 127.0.0.1:80 blocked"));
}

TEST(ResultTest, StatusViolationUnknown) {
  Result result;
  result.SetExitStatusCode(Result::VIOLATION, 0);
  EXPECT_THAT(result.ToString(),
              HasSubstr("SYSCALL VIOLATION - Unknown Violation"));
}

TEST(ResultTest, StackTraceSingleThread) {
  Result result;
  result.set_stack_trace({"frame0", "frame1", "frame2"});
  EXPECT_THAT(result.GetStackTrace(), Eq("frame0 frame1 frame2"));
}

TEST(ResultTest, StackTraceMultipleThreads) {
  Result result;
  result.set_thread_stack_trace({
      {100, {"f0", "f1"}},
      {101, {"g0", "g1"}},
  });
  EXPECT_THAT(result.GetStackTrace(),
              Eq("Task ID [100]: f0 f1 Task ID [101]: g0 g1"));
}

}  // namespace
}  // namespace sandbox2
