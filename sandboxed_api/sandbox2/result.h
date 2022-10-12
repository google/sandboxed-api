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

// This file defines the sandbox2::Result class which will in future handle both
// exit status of the sandboxed process, and possible results returned from it.

#ifndef SANDBOXED_API_SANDBOX2_RESULT_H_
#define SANDBOXED_API_SANDBOX2_RESULT_H_

#include <sys/resource.h>
#include <sys/types.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/regs.h"
#include "sandboxed_api/sandbox2/syscall.h"

namespace sandbox2 {

class Result {
 public:
  // Final execution status.
  enum StatusEnum {
    // Not set yet
    UNSET = 0,
    // OK
    OK,
    // Sandbox initialization failure
    SETUP_ERROR,
    // Syscall violation
    VIOLATION,
    // Process terminated with a signal
    SIGNALED,
    // Process terminated with a timeout
    TIMEOUT,
    // Killed externally by user
    EXTERNAL_KILL,
    // Most likely ptrace() API failed
    INTERNAL_ERROR,
  };

  // Detailed reason codes
  enum ReasonCodeEnum {
    // Codes used by status=`SETUP_ERROR`:
    UNSUPPORTED_ARCH = 0,
    FAILED_TIMERS,
    FAILED_SIGNALS,
    FAILED_SUBPROCESS,
    FAILED_NOTIFY,
    FAILED_CONNECTION,
    FAILED_WAIT,
    FAILED_NAMESPACES,
    FAILED_PTRACE,
    FAILED_IPC,
    FAILED_LIMITS,
    FAILED_CWD,
    FAILED_POLICY,

    // Codes used by status=`INTERNAL_ERROR`:
    FAILED_STORE,
    FAILED_FETCH,
    FAILED_GETEVENT,
    FAILED_MONITOR,
    FAILED_KILL,
    FAILED_INTERRUPT,
    FAILED_CHILD,
    FAILED_INSPECT,

    // TODO(wiktorg) not used currently (syscall number stored insted) - need to
    // fix clients first
    // Codes used by status=`VIOLATION`:
    VIOLATION_SYSCALL,
    VIOLATION_ARCH,
    VIOLATION_NETWORK = 0x10000000,  // TODO(eternalred): temporary value, needs
                                     // to be big until it's fixed
  };

  Result() = default;
  Result(const Result& other) { *this = other; }
  Result& operator=(const Result& other);
  Result(Result&&) = default;
  Result& operator=(Result&&) = default;

  void IgnoreResult() const {}

  // Setters/getters for the final status/code value.
  void SetExitStatusCode(StatusEnum final_status, uintptr_t reason_code) {
    // Don't overwrite exit status codes.
    if (final_status_ != UNSET) {
      return;
    }
    final_status_ = final_status;
    reason_code_ = reason_code;
  }

  // Sets the stack trace.
  // The stacktrace must be sometimes fetched before SetExitStatusCode is
  // called, because after WIFEXITED() or WIFSIGNALED() the process is just a
  // zombie.
  void set_stack_trace(std::vector<std::string> value) {
    stack_trace_ = std::move(value);
  }

  void SetRegs(std::unique_ptr<Regs> regs) { regs_ = std::move(regs); }

  void SetSyscall(std::unique_ptr<Syscall> syscall) {
    syscall_ = std::move(syscall);
  }

  void SetNetworkViolation(std::string network_violation) {
    network_violation_ = std::move(network_violation);
  }

  StatusEnum final_status() const { return final_status_; }
  uintptr_t reason_code() const { return reason_code_; }

  // If true, indicates that the non-OK status is transient and a retry might
  // succeed.
  bool IsRetryable() const { return false; }

  // Returns the current syscall architecture.
  // Client architecture when final_status_ == VIOLATION, might be different
  // from the host architecture (32-bit vs 64-bit syscalls).
  sapi::cpu::Architecture GetSyscallArch() const {
    return syscall_ ? syscall_->arch() : sapi::cpu::kUnknown;
  }

  const std::vector<std::string>& stack_trace() const { return stack_trace_; }

  // Returns the stack trace as a space-delimited string.
  std::string GetStackTrace() const;

  const Regs* GetRegs() const { return regs_.get(); }

  const Syscall* GetSyscall() const { return syscall_.get(); }

  const std::string& GetProgName() const { return prog_name_; }

  const std::string& GetNetworkViolation() const { return network_violation_; }

  void SetProgName(const std::string& name) { prog_name_ = name; }

  const std::string& GetProcMaps() const { return proc_maps_; }

  void SetProcMaps(const std::string& proc_maps) { proc_maps_ = proc_maps; }

  // Converts this result to a absl::Status object.  The status will only be
  // OK if the sandbox process exited normally with an exit code of 0.
  absl::Status ToStatus() const;

  // Returns a descriptive string for final result.
  std::string ToString() const;

  // Converts StatusEnum to a string.
  static std::string StatusEnumToString(StatusEnum value);

  // Converts ReasonCodeEnum to a string.
  static std::string ReasonCodeEnumToString(ReasonCodeEnum value);

  rusage* GetRUsageMonitor() { return &rusage_monitor_; }

 private:
  // Final execution status - see 'StatusEnum' for details.
  StatusEnum final_status_ = UNSET;
  // Termination cause:
  //  a). process exit value if final_status_ == OK,
  //  b). terminating signal if final_status_ == SIGNALED,
  //  c). violating syscall if final_status_ == VIOLATION,
  //  unspecified for the rest of status_ values.
  uintptr_t reason_code_ = 0;
  // Might contain stack-trace of the process, especially if it failed with
  // syscall violation, or was terminated by a signal.
  std::vector<std::string> stack_trace_;
  // Might contain the register values of the process, similar to the stack.
  // trace
  std::unique_ptr<Regs> regs_;
  // Might contain violating syscall information
  std::unique_ptr<Syscall> syscall_;
  // Name of the process (as it can not be accessed anymore after termination).
  std::string prog_name_;
  // /proc/pid/maps of the main process.
  std::string proc_maps_;
  // IP and port if network violation occurred
  std::string network_violation_;
  // Final resource usage as defined in <sys/resource.h> (man getrusage), for
  // the Monitor thread.
  rusage rusage_monitor_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_RESULT_H_
