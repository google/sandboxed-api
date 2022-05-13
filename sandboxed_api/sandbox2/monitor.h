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

// The sandbox2::Monitor class is responsible for tracking the processes, and
// displaying their current statuses (syscalls, states, violations).

#ifndef SANDBOXED_API_SANDBOX2_MONITOR_H_
#define SANDBOXED_API_SANDBOX2_MONITOR_H_

#include <sys/resource.h>

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <memory>
#include <thread>

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/notification.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/ipc.h"
#include "sandboxed_api/sandbox2/network_proxy/server.h"
#include "sandboxed_api/sandbox2/notify.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/regs.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/syscall.h"

namespace sandbox2 {

class Monitor final {
 public:
  // executor, policy and notify are not owned by the Monitor
  Monitor(Executor* executor, Policy* policy, Notify* notify);

  Monitor(const Monitor&) = delete;
  Monitor& operator=(const Monitor&) = delete;

  ~Monitor();

 private:
  friend class Sandbox2;

  // Timeout used with sigtimedwait (0.5s).
  static constexpr int kWakeUpPeriodSec = 0L;
  static constexpr int kWakeUpPeriodNSec = (500L * 1000L * 1000L);

  // Starts the Monitor.
  void Run();

  // Getters for private fields.
  bool IsDone() const { return done_notification_.HasBeenNotified(); }

  // Getter/Setter for wait_for_execve_.
  bool IsActivelyMonitoring();
  void SetActivelyMonitoring();

  // Sends Policy to the Client.
  // Returns success/failure status.
  bool InitSendPolicy();

  // Waits for the SandboxReady signal from the client.
  // Returns success/failure status.
  bool WaitForSandboxReady();

  // ptrace(PTRACE_SEIZE) to the Client.
  // Returns success/failure status.
  bool InitPtraceAttach();

  // Sets up required signal masks/handlers; prepare mask for sigtimedwait().
  bool InitSetupSignals(sigset_t* sset);

  // Sends information about data exchange channels.
  bool InitSendIPC();

  // Sends information about the current working directory.
  bool InitSendCwd();

  // Applies limits on the sandboxee.
  bool InitApplyLimits();

  // Applies individual limit on the sandboxee.
  bool InitApplyLimit(pid_t pid, int resource, const rlimit64& rlim) const;

  // Kills the main traced PID with PTRACE_KILL.
  // Returns false if an error occured and process could not be killed.
  bool KillSandboxee();

  // Interrupts the main traced PID with PTRACE_INTERRUPT.
  // Returns false if an error occured and process could not be interrupted.
  bool InterruptSandboxee();

  // Waits for events from monitored clients and signals from the main process.
  void MainLoop(sigset_t* sset);

  // Process with given PID changed state to a stopped state.
  void StateProcessStopped(pid_t pid, int status);

  // Logs the syscall violation and kills the process afterwards.
  void ActionProcessSyscallViolation(Regs* regs, const Syscall& syscall,
                                     ViolationType violation_type);

  // PID called a traced syscall, or was killed due to syscall.
  void ActionProcessSyscall(Regs* regs, const Syscall& syscall);

  // Sets basic info status and reason code in the result object.
  void SetExitStatusCode(Result::StatusEnum final_status,
                         uintptr_t reason_code);
  // Whether a stack trace should be collected given the current status
  bool ShouldCollectStackTrace();
  // Sets additional information in the result object, such as program name,
  // stack trace etc.
  void SetAdditionalResultInfo(std::unique_ptr<Regs> regs);

  // Logs a SANDBOX VIOLATION message based on the registers and additional
  // explanation for the reason of the violation.
  void LogSyscallViolation(const Syscall& syscall) const;
  // Logs an additional explanation for the possible reason of the violation
  // based on the registers.
  void LogSyscallViolationExplanation(const Syscall& syscall) const;

  // Ptrace events:
  // Syscall violation processing path.
  void EventPtraceSeccomp(pid_t pid, int event_msg);

  // Processes exit path.
  void EventPtraceExit(pid_t pid, int event_msg);

  // Processes fork/vfork/clone path.
  void EventPtraceNewProcess(pid_t pid, int event_msg);

  // Processes execution path.
  void EventPtraceExec(pid_t pid, int event_msg);

  // Processes stop path.
  void EventPtraceStop(pid_t pid, int stopsig);

  // Processes syscall exit.
  void EventSyscallExit(pid_t pid);

  // Enable network proxy server, this will start a thread in the sandbox
  // that waits for connection requests from the sandboxee.
  void EnableNetworkProxyServer();

  // Internal objects, owned by the Sandbox2 object.
  Executor* executor_;
  Notify* notify_;
  Policy* policy_;
  Result result_;
  // Comms channel ptr, copied from the Executor object for convenience.
  Comms* comms_;
  // IPC ptr, used for exchanging data with the sandboxee.
  IPC* ipc_;

  // Parent (the Sandbox2 object) waits on it, until we either enable
  // monitoring of a process (sandboxee) successfully, or the setup process
  // fails.
  absl::Notification setup_notification_;
  // The field indicates whether the sandboxing task has been completed (either
  // successfully or with error).
  absl::Notification done_notification_;

  // The main tracked PID.
  pid_t pid_ = -1;

  // False iff external kill is requested
  std::atomic_flag external_kill_request_flag_ = ATOMIC_FLAG_INIT;
  // False iff dump stack is requested
  std::atomic_flag dump_stack_request_flag_ = ATOMIC_FLAG_INIT;
  // Deadline in Unix millis
  std::atomic<int64_t> deadline_millis_{0};
  // Was external kill sent to the sandboxee
  bool external_kill_ = false;
  // Network violation occurred and process of killing sandboxee started
  bool network_violation_ = false;
  // Is the sandboxee timed out
  bool timed_out_ = false;
  // Should we dump the main sandboxed PID's stack?
  bool should_dump_stack_ = false;

  // Is the sandboxee actively monitored, or maybe we're waiting for execve()?
  bool wait_for_execve_;
  // Log file specified by
  // --sandbox_danger_danger_permit_all_and_log flag.
  FILE* log_file_ = nullptr;

  // Empty temp file used for mapping the comms fd when the Tomoyo LSM is
  // active.
  std::string comms_fd_dev_;

  // Handle to the class responsible for proxying and validating connect()
  // requests.
  std::unique_ptr<NetworkProxyServer> network_proxy_server_;

  std::thread network_proxy_thread_;

  // Syscalls that are running, whose result values we want to inspect.
  absl::flat_hash_map<pid_t, Syscall> syscalls_in_progress_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_MONITOR_H_
