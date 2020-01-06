// Copyright 2020 Google LLC. All Rights Reserved.
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

// The sandbox2::Notify class handless exceptional situations in the sandbox

#ifndef SANDBOXED_API_SANDBOX2_NOTIFY_H_
#define SANDBOXED_API_SANDBOX2_NOTIFY_H_

#include <sys/types.h>

#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/syscall.h"

namespace sandbox2 {

enum ViolationType {
  // A syscall disallowed by the policy was invoked.
  kSyscallViolation,
  // A syscall with cpu architecture not covered by the policy was invoked.
  kArchitectureSwitchViolation,
};

class Notify {
 public:
  virtual ~Notify() = default;

  // Called when a process has been created and executed, but not yet sandboxed.
  // Using comms only makes sense if the client is sandboxed in the
  // Executor::set_enable_sandbox_before_exec(false) mode.
  // Returns a success indicator: false will cause the Sandbox Monitor to return
  // sandbox2::Result::SETUP_ERROR for Run()/RunAsync().
  virtual bool EventStarted(pid_t pid, Comms* comms) { return true; }

  // Called when all sandboxed processes finished.
  virtual void EventFinished(const Result& result) {}

  // Called when a process exited with a syscall violation.
  virtual void EventSyscallViolation(const Syscall& syscall,
                                     ViolationType type) {}

  // Called when a policy called TRACE. The syscall is allowed if this method
  // returns true.
  // This allows for implementing 'log, but allow' policies.
  virtual bool EventSyscallTrap(const Syscall& syscall) { return false; }

  // Called when a process received a signal.
  virtual void EventSignal(pid_t pid, int sig_no) {}
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_NOTIFY_H_
