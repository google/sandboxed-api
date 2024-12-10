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

#ifndef SANDBOXED_API_SANDBOX2_MONITOR_PTRACE_H_
#define SANDBOXED_API_SANDBOX2_MONITOR_PTRACE_H_

#include <atomic>
#include <cstdint>
#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/monitor_base.h"
#include "sandboxed_api/sandbox2/notify.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/regs.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/sandbox2/util/pid_waiter.h"
#include "sandboxed_api/util/thread.h"

namespace sandbox2 {

class PtraceMonitor : public MonitorBase {
 public:
  PtraceMonitor(Executor* executor, Policy* policy, Notify* notify);
  ~PtraceMonitor() { Join(); }

  void Kill() override {
    external_kill_request_flag_.clear(std::memory_order_relaxed);
    NotifyMonitor();
  }

  void DumpStackTrace() override {
    dump_stack_request_flag_.clear(std::memory_order_relaxed);
    NotifyMonitor();
  }

  void SetWallTimeLimit(absl::Duration limit) override {
    if (limit == absl::ZeroDuration()) {
      VLOG(1) << "Disarming walltime timer to ";
      deadline_millis_.store(0, std::memory_order_relaxed);
    } else {
      VLOG(1) << "Will set the walltime timer to " << limit;
      absl::Time deadline = absl::Now() + limit;
      deadline_millis_.store(absl::ToUnixMillis(deadline),
                             std::memory_order_relaxed);
      NotifyMonitor();
    }
  }

 private:
  // Waits for events from monitored clients and signals from the main process.
  void RunInternal() override;
  void Join() override;
  void Run();

  void NotifyNetworkViolation() override { NotifyMonitor(); }

  // Notifies monitor about a state change
  void NotifyMonitor();

  // PID called a traced syscall, or was killed due to syscall.
  void ActionProcessSyscall(Regs* regs, const Syscall& syscall);

  // Getter/Setter for wait_for_execve_.
  bool IsActivelyMonitoring();
  void SetActivelyMonitoring();

  // Process with given PID changed state to a stopped state.
  void StateProcessStopped(pid_t pid, int status);

  // Sets additional information in the result object, such as program name,
  // stack trace etc.
  void SetAdditionalResultInfo(std::unique_ptr<Regs> regs);

  // Logs the syscall violation and kills the process afterwards.
  void ActionProcessSyscallViolation(Regs* regs, const Syscall& syscall,
                                     ViolationType violation_type);

  void LogStackTraceOfPid(pid_t pid);

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

  // Kills the main traced PID with PTRACE_KILL.
  // Returns false if an error occurred and process could not be killed.
  bool KillSandboxee();

  // Interrupts the main traced PID with PTRACE_INTERRUPT.
  // Returns false if an error occurred and process could not be interrupted.
  bool InterruptSandboxee();

  // ptrace(PTRACE_SEIZE) to the Client.
  // Returns success/failure status.
  bool InitPtraceAttach();

  // Parent (the Sandbox2 object) waits on it, until we either enable
  // monitoring of a process (sandboxee) successfully, or the setup process
  // fails.
  absl::Notification setup_notification_;
  // Deadline in Unix millis
  std::atomic<int64_t> deadline_millis_{0};
  // False iff external kill is requested
  std::atomic_flag external_kill_request_flag_ = ATOMIC_FLAG_INIT;
  // False iff dump stack is requested
  std::atomic_flag dump_stack_request_flag_ = ATOMIC_FLAG_INIT;
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
  // Syscalls that are running, whose result values we want to inspect.
  absl::flat_hash_map<pid_t, Syscall> syscalls_in_progress_;
  sigset_t sset_;
  // Deadline after which sandboxee get terminated via PTRACE_O_EXITKILL.
  absl::Time hard_deadline_ = absl::InfiniteFuture();
  // PidWaiter for waiting for sandboxee events.
  PidWaiter pid_waiter_;

  // Monitor thread object.
  sapi::Thread thread_;

  // Synchronizes deadline setting and notifying the monitor.
  absl::Mutex notify_mutex_;
  // True iff monitor thread is notified
  bool notified_ ABSL_GUARDED_BY(notify_mutex_) = false;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_MONITOR_BASE_H_
