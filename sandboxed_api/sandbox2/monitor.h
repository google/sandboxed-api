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

#include "absl/synchronization/blocking_counter.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/ipc.h"
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

  // As per 'man 7 pthreads' pthreads uses first three RT signals, so we use
  // something safe here (but still lower than __SIGRTMAX).
  //
  // A signal which makes wait() to exit due to interrupt, so the Monitor can
  // check whether it should terminate.
  static const int kExternalKillSignal = (__SIGRTMIN + 10);
  // A signal which system timer delivers in case the wall-time timer limit was
  // reached.
  static const int kTimerWallTimeSignal = (__SIGRTMIN + 12);
  // A signal which makes Monitor to arm its wall-time timer.
  static const int kTimerSetSignal = (__SIGRTMIN + 13);
  // Dump the main sandboxed process's stack trace to log.
  static const int kDumpStackSignal = (__SIGRTMIN + 14);
#if ((__SIGRTMIN + 14) > __SIGRTMAX)
#error "sandbox2::Monitor exceeding > __SIGRTMAX)"
#endif

  // Timeout used with sigtimedwait (0.5s).
  static const int kWakeUpPeriodSec = 0L;
  static const int kWakeUpPeriodNSec = (500L * 1000L * 1000L);

  // Starts the Monitor.
  void Run();

  // Getters for private fields.
  bool IsDone() const { return done_.load(std::memory_order_acquire); }

  // Getter/Setter for wait_for_execve_.
  bool IsActivelyMonitoring();
  void SetActivelyMonitoring();

  // Waits for events from monitored clients and signals from the main process.
  void MainLoop(sigset_t* sset);

  // Analyzes signals which Monitor might have already received.
  void MainSignals(int signo, siginfo_t* si);

  // Analyzes any possible children process status changes; returns 'true' if
  // there are no more processes to track.
  bool MainWait();

  // Sends Policy to the Client.
  // Returns success/failure status.
  bool InitSendPolicy();

  // Waits for the SandboxReady signal from the client.
  // Returns success/failure status.
  bool WaitForSandboxReady();

  // ptrace(PTRACE_SEIZE) to the Client.
  // Returns success/failure status.
  bool InitPtraceAttach();

  // Waits for the Client to connect.
  // Returns success/failure status.
  bool InitAcceptConnection();

  // Sets up required signal masks/handlers; prepare mask for sigtimedwait().
  bool InitSetupSignals(sigset_t* sset);

  // Sets up a given signal; modify the sigmask used with sigtimedwait().
  bool InitSetupSig(int signo, sigset_t* sset);

  // Sends information about data exchange channels.
  bool InitSendIPC();

  // Sends information about the current working directory.
  bool InitSendCwd();

  // Applies limits on the sandboxee.
  bool InitApplyLimits();

  // Applies individual limit on the sandboxee.
  bool InitApplyLimit(pid_t pid, __rlimit_resource resource,
                      const rlimit64& rlim) const;

  // Creates timers.
  bool InitSetupTimer();

  // Deletes timers.
  void CleanUpTimer();

  // Arms the walltime timer, absl::ZeroDuration() disarms the timer.
  bool TimerArm(absl::Duration duration);

  // Final action with regard to PID.
  // Continues PID with an optional signal.
  void ActionProcessContinue(pid_t pid, int signo);

  // Stops the PID with an optional signal.
  void ActionProcessStop(pid_t pid, int signo);

  // Logs the syscall violation and kills the process afterwards.
  void ActionProcessSyscallViolation(Regs* regs, const Syscall& syscall,
                                     ViolationType violation_type);

  // Prints a SANDBOX VIOLATION message based on the registers.
  // If the registers match something disallowed by Policy::GetDefaultPolicy,
  // then it also prints a additional description of the reason.
  void LogAccessViolation(const Syscall& syscall);

  // PID called a syscall, or was killed due to syscall.
  void ActionProcessSyscall(Regs* regs, const Syscall& syscall);

  // Kills the PID with PTRACE_KILL.
  void ActionProcessKill(pid_t pid, Result::StatusEnum status, uintptr_t code);

  // Ptrace events:
  // Syscall violation processing path.
  void EventPtraceSeccomp(pid_t pid, int event_msg);

  // Processes exit path.
  void EventPtraceExit(pid_t pid, int event_msg);

  // Processes excution path.
  void EventPtraceExec(pid_t pid, int event_msg);

  // Processes stop path.
  void EventPtraceStop(pid_t pid, int stopsig);

  // Changes the state of a given PID:
  // Process is in a stopped state.
  void StateProcessStopped(pid_t pid, int status);

  // Helpers operating on PIDs.
  // Interrupts the PID.
  void PidInterrupt(pid_t pid);

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
  std::unique_ptr<absl::BlockingCounter> setup_counter_;
  // The Wall-Time timer for traced processes.
  std::unique_ptr<timer_t> walltime_timer_;

  // The main tracked PID.
  pid_t pid_ = -1;

  // The field indicates whether the sandboxing task has been completed (either
  // successfully or with error).
  std::atomic<bool> done_;
  absl::Mutex done_mutex_;
  // Should we dump the main sandboxed PID's stack?
  bool should_dump_stack_ = false;

  // Is the sandboxee actively monitored, or maybe we're waiting for execve()?
  bool wait_for_execve_;
  // Log file specified by
  // --sandbox_danger_danger_permit_all_and_log flag.
  FILE* log_file_ = nullptr;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_MONITOR_H_
