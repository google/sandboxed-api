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

#include <sys/resource.h>

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/sandbox2/util.h"

namespace sandbox2 {

Result& Result::operator=(const Result& other) {
  final_status_ = other.final_status_;
  reason_code_ = other.reason_code_;
  stack_trace_ = other.stack_trace_;
  thread_stack_traces_ = other.thread_stack_traces_;
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
  rusage_sandboxee_ = other.rusage_sandboxee_;
  return *this;
}

std::string Result::StatusEnumToString(StatusEnum value) {
  switch (value) {
    case UNSET:
      return "UNSET";
    case OK:
      return "OK";
    case SETUP_ERROR:
      return "SETUP_ERROR";
    case VIOLATION:
      return "VIOLATION";
    case SIGNALED:
      return "SIGNALED";
    case TIMEOUT:
      return "TIMEOUT";
    case EXTERNAL_KILL:
      return "EXTERNAL_KILL";
    case INTERNAL_ERROR:
      return "INTERNAL_ERROR";
  }
  return "UNKNOWN";
}

std::string Result::ReasonCodeEnumToString(ReasonCodeEnum value) {
  switch (value) {
    case UNSUPPORTED_ARCH:
      return "UNSUPPORTED_ARCH";
    case FAILED_TIMERS:
      return "FAILED_TIMERS";
    case FAILED_SIGNALS:
      return "FAILED_SIGNALS";
    case FAILED_SUBPROCESS:
      return "FAILED_SUBPROCESS";
    case FAILED_NOTIFY:
      return "FAILED_NOTIFY";
    case FAILED_CONNECTION:
      return "FAILED_CONNECTION";
    case FAILED_WAIT:
      return "FAILED_WAIT";
    case FAILED_NAMESPACES:
      return "FAILED_NAMESPACES";
    case FAILED_PTRACE:
      return "FAILED_PTRACE";
    case FAILED_IPC:
      return "FAILED_IPC";
    case FAILED_LIMITS:
      return "FAILED_LIMITS";
    case FAILED_CWD:
      return "FAILED_CWD";
    case FAILED_POLICY:
      return "FAILED_POLICY";
    case FAILED_VERSION_CHECK:
      return "FAILED_VERSION_CHECK";
    case FAILED_STORE:
      return "FAILED_STORE";
    case FAILED_FETCH:
      return "FAILED_FETCH";
    case FAILED_GETEVENT:
      return "FAILED_GETEVENT";
    case FAILED_MONITOR:
      return "FAILED_MONITOR";
    case FAILED_KILL:
      return "FAILED_KILL";
    case FAILED_INTERRUPT:
      return "FAILED_INTERRUPT";
    case FAILED_CHILD:
      return "FAILED_CHILD";
    case FAILED_INSPECT:
      return "FAILED_INSPECT";
    case VIOLATION_SYSCALL:
      return "VIOLATION_SYSCALL";
    case VIOLATION_ARCH:
      return "VIOLATION_ARCH";
    case VIOLATION_NETWORK:
      return "VIOLATION_NETWORK";
    case FAILED_COMMS_UPGRADE:
      return "FAILED_COMMS_UPGRADE";
    case FAILED_CONFIG:
      return "FAILED_CONFIG";
  }
  return absl::StrCat("UNKNOWN: ", value);
}

std::string Result::GetStackTrace() const {
  if (!thread_stack_traces_.empty()) {
    std::vector<std::string> res;
    res.reserve(thread_stack_traces_.size());
    for (const auto& [tid, stack_trace] : thread_stack_traces_) {
      res.push_back(absl::StrFormat("Task ID [%d]: %s", tid,
                                    absl::StrJoin(stack_trace, " ")));
    }
    return absl::StrJoin(res, " ");
  }
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
    case UNSET:
      result = absl::StrCat("UNSET - Code: ", reason_code());
      break;
    case OK:
      result = absl::StrCat("OK - Exit code: ", reason_code());
      break;
    case SETUP_ERROR:
      result = absl::StrCat(
          "SETUP_ERROR - Code: ",
          ReasonCodeEnumToString(static_cast<ReasonCodeEnum>(reason_code())));
      break;
    case VIOLATION:
      if (syscall_) {
        result = absl::StrCat("SYSCALL VIOLATION - Violating Syscall ",
                              syscall_->GetDescription(),
                              " Stack: ", GetStackTrace());
      } else if (reason_code() == VIOLATION_NETWORK) {
        result = absl::StrCat("NETWORK VIOLATION: ", GetNetworkViolation());
      } else {
        result = "SYSCALL VIOLATION - Unknown Violation";
      }
      break;
    case SIGNALED:
      result = absl::StrCat("Process terminated with a SIGNAL - Signal: ",
                            util::GetSignalName(reason_code()),
                            " Stack: ", GetStackTrace());
      break;
    case TIMEOUT:
      result = absl::StrCat("Process TIMEOUT - Code: ", reason_code(),
                            " Stack: ", GetStackTrace());
      break;
    case EXTERNAL_KILL:
      result = absl::StrCat("Process killed by user - Code: ", reason_code(),
                            " Stack: ", GetStackTrace());
      break;
    case INTERNAL_ERROR:
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

}  // namespace sandbox2
