// Copyright 2023 Google LLC
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

// The sandbox2::Monitor class is responsible for tracking the processes, and
// displaying their current statuses (syscalls, states, violations).

#ifndef SANDBOXED_API_SANDBOX2_MONITOR_BASE_H_
#define SANDBOXED_API_SANDBOX2_MONITOR_BASE_H_

#include <sys/resource.h>
#include <sys/types.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/fork_client.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/sandbox2/ipc.h"
#include "sandboxed_api/sandbox2/network_proxy/server.h"
#include "sandboxed_api/sandbox2/notify.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/regs.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/util/thread.h"

namespace sandbox2 {

class MonitorBase {
 public:
  // executor, policy and notify are not owned by the Monitor
  MonitorBase(Executor* executor, Policy* policy, Notify* notify);

  MonitorBase(const MonitorBase&) = delete;
  MonitorBase& operator=(const MonitorBase&) = delete;

  virtual ~MonitorBase();

  // Starts the Monitor.
  void Launch();

  // Getters for private fields.
  bool IsDone() const { return done_notification_.HasBeenNotified(); }

  // Enable network proxy server, this will start a thread in the sandbox
  // that waits for connection requests from the sandboxee.
  void EnableNetworkProxyServer();

  // Notifies the monitor that a network violation occurred.
  virtual void NotifyNetworkViolation() = 0;

  pid_t pid() const { return process_.main_pid; }

  const Result& result() const { return result_; }

  absl::StatusOr<Result> AwaitResultWithTimeout(absl::Duration timeout);

  virtual void Kill() = 0;
  virtual void DumpStackTrace() = 0;
  virtual void SetWallTimeLimit(absl::Duration limit) = 0;

 protected:
  // Sends the policy to the client.
  // Can be overridden by subclasses to save/modify policy before sending.
  // Returns success/failure status.
  virtual absl::Status SendPolicy(const std::vector<sock_filter>& policy);

  bool wait_for_execveat() const { return wait_for_execveat_; }
  void set_wait_for_execveat(bool wait_for_execve) {
    wait_for_execveat_ = wait_for_execve;
  }

  void OnDone();

  // Sends a message to the client that we're ready to monitor it.
  // The message contains the monitor type and final sandboxee mode flags
  // (currently only flag to allow speculation for the seccomped process).
  bool SendMonitorReadyMessageAndFlags(uint32_t monitor_type);

  // Sets basic info status and reason code in the result object.
  void SetExitStatusCode(Result::StatusEnum final_status,
                         uintptr_t reason_code);

  // Logs a SANDBOX VIOLATION message based on the registers and additional
  // explanation for the reason of the violation.
  void LogSyscallViolation(const Syscall& syscall) const;

  // Tells if collecting stack trace is at all possible.
  bool StackTraceCollectionPossible() const;

  // Whether a stack trace should be collected given the current status
  bool ShouldCollectStackTrace(Result::StatusEnum status) const;

  // Gets stack trace.
  absl::StatusOr<std::vector<std::string>> GetStackTrace(const Regs* regs);

  // Gets and logs stack trace.
  absl::StatusOr<std::vector<std::string>> GetAndLogStackTrace(
      const Regs* regs);

  // Internal objects, owned by the Sandbox2 object.
  Executor* executor_;
  Policy* policy_;
  Notify* notify_;

  // The sandboxee process.
  SandboxeeProcess process_;
  Result result_;
  // Comms channel ptr, copied from the Executor object for convenience.
  Comms* comms_;
  // Log file specified by
  // --sandbox_danger_danger_permit_all_and_log flag.
  FILE* log_file_ = nullptr;
  // Handle to the class responsible for proxying and validating connect()
  // requests.
  std::unique_ptr<NetworkProxyServer> network_proxy_server_;
  // Monitor type
  MonitorType type_ = FORKSERVER_MONITOR_PTRACE;

 private:
  // Instantiates and sends Policy to the Client.
  // Returns success/failure status.
  bool InitSendPolicy();

  // Waits for the SandboxReady signal from the client.
  // Returns success/failure status.
  bool WaitForSandboxReady();

  // Sends information about data exchange channels.
  bool InitSendIPC();

  // Sends information about the current working directory.
  bool InitSendCwd();

  // Applies limits on the sandboxee.
  bool InitApplyLimits();

  // Applies individual limit on the sandboxee.
  bool InitApplyLimit(pid_t pid, int resource, const rlimit64& rlim) const;

  // Logs an additional explanation for the possible reason of the violation
  // based on the registers.
  void LogSyscallViolationExplanation(const Syscall& syscall) const;

  virtual void RunInternal() = 0;
  virtual void Join() = 0;

  // IPC ptr, used for exchanging data with the sandboxee.
  IPC* ipc_;

  // The field indicates whether the sandboxing task has been completed (either
  // successfully or with error).
  absl::Notification done_notification_;

  // Empty temp file used for mapping the comms fd when the Tomoyo LSM is
  // active.
  std::string comms_fd_dev_;

  sapi::Thread network_proxy_thread_;

  // Is the sandboxee forked from a custom forkserver?
  bool uses_custom_forkserver_;

  // Are we waiting for the first execveat syscall?
  bool wait_for_execveat_ = false;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_MONITOR_BASE_H_
