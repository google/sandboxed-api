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

// Implementation of the sandbox2::Result class.

#include "sandboxed_api/sandbox2/result.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/sandbox2/util.h"

namespace sandbox2 {

Result& Result::operator=(const Result& other) {
  final_status_ = other.final_status_;
  reason_code_ = other.reason_code_;
  stack_trace_ = other.stack_trace_;
  if (other.regs_) {
    regs_ = std::make_unique<Regs>(*other.regs_);
  } else {
    regs_.reset(nullptr);
  }
  if (other.syscall_) {
    syscall_ = std::make_unique<Syscall>(*other.syscall_);
  } else {
    syscall_.reset(nullptr);
  }
  prog_name_ = other.prog_name_;
  proc_maps_ = other.proc_maps_;
  rusage_monitor_ = other.rusage_monitor_;
  return *this;
}

std::string Result::GetStackTrace() const {
  return absl::StrJoin(stack_trace_, " ");
}

absl::Status Result::ToStatus() const {
  switch (final_status()) {
    case OK:
      if (reason_code() == 0) {
        return absl::OkStatus();
      }
      break;
    case TIMEOUT:
      return absl::DeadlineExceededError(ToString());
    default:
      break;
  }
  return absl::InternalError(ToString());
}

std::string Result::ToString() const {
  std::string result;
  switch (final_status()) {
    case sandbox2::Result::UNSET:
      result = absl::StrCat("UNSET - Code: ", reason_code());
      break;
    case sandbox2::Result::OK:
      result = absl::StrCat("OK - Exit code: ", reason_code());
      break;
    case sandbox2::Result::SETUP_ERROR:
      result = absl::StrCat(
          "SETUP_ERROR - Code: ",
          ReasonCodeEnumToString(static_cast<ReasonCodeEnum>(reason_code())));
      break;
    case sandbox2::Result::VIOLATION:
      if (reason_code() == sandbox2::Result::VIOLATION_NETWORK) {
        result = absl::StrCat("NETWORK VIOLATION: ", GetNetworkViolation());
      } else {
        result = absl::StrCat(
            "SYSCALL VIOLATION - Violating Syscall ",
            Syscall::GetArchDescription(GetSyscallArch()), "[", reason_code(),
            "/", Syscall(GetSyscallArch(), reason_code()).GetName(),
            "] Stack: ", GetStackTrace());
      }
      break;
    case sandbox2::Result::SIGNALED:
      result = absl::StrCat("Process terminated with a SIGNAL - Signal: ",
                            util::GetSignalName(reason_code()),
                            " Stack: ", GetStackTrace());
      break;
    case sandbox2::Result::TIMEOUT:
      result = absl::StrCat("Process TIMEOUT - Code: ", reason_code(),
                            " Stack: ", GetStackTrace());
      break;
    case sandbox2::Result::EXTERNAL_KILL:
      result = absl::StrCat("Process killed by user - Code: ", reason_code(),
                            " Stack: ", GetStackTrace());
      break;
    case sandbox2::Result::INTERNAL_ERROR:
      result = absl::StrCat(
          "INTERNAL_ERROR - Code: ",
          ReasonCodeEnumToString(static_cast<ReasonCodeEnum>(reason_code())));
      break;
    default:
      result =
          absl::StrCat("<UNKNOWN>(", final_status(), ") Code: ", reason_code());
  }
  if constexpr (sapi::sanitizers::IsAny()) {
    absl::StrAppend(
        &result,
        " - Warning: this executor is built with ASAN, MSAN or TSAN, chances "
        "are the sandboxee is too, which is incompatible with sandboxing.");
  } else {
    if (
        getenv("COVERAGE") != nullptr) {
      absl::StrAppend(
          &result,
          " - Warning: this executor is built with coverage enabled, chances "
          "are the sandboxee too, which is incompatible with sandboxing.");
    }
  }
  return result;
}

std::string Result::StatusEnumToString(StatusEnum value) {
  switch (value) {
    case sandbox2::Result::UNSET:
      return "UNSET";
    case sandbox2::Result::OK:
      return "OK";
    case sandbox2::Result::SETUP_ERROR:
      return "SETUP_ERROR";
    case sandbox2::Result::VIOLATION:
      return "VIOLATION";
    case sandbox2::Result::SIGNALED:
      return "SIGNALED";
    case sandbox2::Result::TIMEOUT:
      return "TIMEOUT";
    case sandbox2::Result::EXTERNAL_KILL:
      return "EXTERNAL_KILL";
    case sandbox2::Result::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
  }
  return "UNKNOWN";
}

std::string Result::ReasonCodeEnumToString(ReasonCodeEnum value) {
  switch (value) {
    case sandbox2::Result::UNSUPPORTED_ARCH:
      return "UNSUPPORTED_ARCH";
    case sandbox2::Result::FAILED_TIMERS:
      return "FAILED_TIMERS";
    case sandbox2::Result::FAILED_SIGNALS:
      return "FAILED_SIGNALS";
    case sandbox2::Result::FAILED_SUBPROCESS:
      return "FAILED_SUBPROCESS";
    case sandbox2::Result::FAILED_NOTIFY:
      return "FAILED_NOTIFY";
    case sandbox2::Result::FAILED_CONNECTION:
      return "FAILED_CONNECTION";
    case sandbox2::Result::FAILED_WAIT:
      return "FAILED_WAIT";
    case sandbox2::Result::FAILED_NAMESPACES:
      return "FAILED_NAMESPACES";
    case sandbox2::Result::FAILED_PTRACE:
      return "FAILED_PTRACE";
    case sandbox2::Result::FAILED_IPC:
      return "FAILED_IPC";
    case sandbox2::Result::FAILED_LIMITS:
      return "FAILED_LIMITS";
    case sandbox2::Result::FAILED_CWD:
      return "FAILED_CWD";
    case sandbox2::Result::FAILED_POLICY:
      return "FAILED_POLICY";
    case sandbox2::Result::FAILED_STORE:
      return "FAILED_STORE";
    case sandbox2::Result::FAILED_FETCH:
      return "FAILED_FETCH";
    case sandbox2::Result::FAILED_GETEVENT:
      return "FAILED_GETEVENT";
    case sandbox2::Result::FAILED_MONITOR:
      return "FAILED_MONITOR";
    case sandbox2::Result::FAILED_KILL:
      return "FAILED_KILL";
    case sandbox2::Result::FAILED_INTERRUPT:
      return "FAILED_INTERRUPT";
    case sandbox2::Result::FAILED_CHILD:
      return "FAILED_CHILD";
    case sandbox2::Result::FAILED_INSPECT:
      return "FAILED_INSPECT";
    case sandbox2::Result::VIOLATION_SYSCALL:
      return "VIOLATION_SYSCALL";
    case sandbox2::Result::VIOLATION_ARCH:
      return "VIOLATION_ARCH";
    case sandbox2::Result::VIOLATION_NETWORK:
      return "VIOLATION_NETWORK";
  }
  return absl::StrCat("UNKNOWN: ", value);
}

}  // namespace sandbox2
