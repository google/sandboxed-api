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

// Implementation file for the sandbox2::PtraceMonitor class.

#include "sandboxed_api/sandbox2/monitor_ptrace.h"

#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <ios>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/optimization.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/log/vlog_is_on.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/flags.h"
#include "sandboxed_api/sandbox2/notify.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/regs.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sanitizer.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/sandbox2/util/pid_waiter.h"
#include "sandboxed_api/util/status_macros.h"
#include "sandboxed_api/util/thread.h"

namespace sandbox2 {
namespace {

// We could use the ProcMapsIterator, however we want the full file content.
std::string ReadProcMaps(pid_t pid) {
  std::ifstream input(absl::StrCat("/proc/", pid, "/maps"),
                      std::ios_base::in | std::ios_base::binary);
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

void ContinueProcess(pid_t pid, int signo) {
  if (ptrace(PTRACE_CONT, pid, 0, signo) == -1) {
    if (errno == ESRCH) {
      LOG(WARNING) << "Process " << pid
                   << " died while trying to PTRACE_CONT it";
    } else {
      PLOG(ERROR) << "ptrace(PTRACE_CONT, pid=" << pid << ", sig=" << signo
                  << ")";
    }
  }
}

void StopProcess(pid_t pid, int signo) {
  if (ptrace(PTRACE_LISTEN, pid, 0, signo) == -1) {
    if (errno == ESRCH) {
      LOG(WARNING) << "Process " << pid
                   << " died while trying to PTRACE_LISTEN it";
    } else {
      PLOG(ERROR) << "ptrace(PTRACE_LISTEN, pid=" << pid << ", sig=" << signo
                  << ")";
    }
  }
}

void CompleteSyscall(pid_t pid, int signo) {
  if (ptrace(PTRACE_SYSCALL, pid, 0, signo) == -1) {
    if (errno == ESRCH) {
      LOG(WARNING) << "Process " << pid
                   << " died while trying to PTRACE_SYSCALL it";
    } else {
      PLOG(ERROR) << "ptrace(PTRACE_SYSCALL, pid=" << pid << ", sig=" << signo
                  << ")";
    }
  }
}

// Waits for the given task to stop, but leaves the task in a waitable state.
// Returns an error if the task is not stopped within the given timeout.
absl::Status WaitForTaskToStop(pid_t pid, absl::Duration timeout) {
  siginfo_t info;
  info.si_pid = 0;
  int ret;
  auto deadline = absl::Now() + timeout;
  do {
    // We need to use waitid() here instead of waitpid() because we can specify
    // WNOWAIT with waitid(), which will leave the task in a waitable state,
    // unlike waitpid().
    ret = waitid(P_PID, pid, &info, WSTOPPED | WNOWAIT | WNOHANG);
  } while (ret == 0 && info.si_pid != pid && absl::Now() < deadline);

  if (ret < 0 || (ret == 0 && info.si_pid != pid)) {
    LOG(ERROR) << "waitid failed: could not wait for task: " << pid;
    return absl::ErrnoToStatus(errno, "waitid failed");
  }

  return absl::OkStatus();
}

}  // namespace

PtraceMonitor::PtraceMonitor(Executor* executor, Policy* policy, Notify* notify)
    : MonitorBase(executor, policy, notify) {
  if (executor_->limits()->wall_time_limit() != absl::ZeroDuration()) {
    auto deadline = absl::Now() + executor_->limits()->wall_time_limit();
    deadline_millis_.store(absl::ToUnixMillis(deadline),
                           std::memory_order_relaxed);
  }
  external_kill_request_flag_.test_and_set(std::memory_order_relaxed);
  dump_stack_request_flag_.test_and_set(std::memory_order_relaxed);
  use_deadline_manager_ =
      absl::GetFlag(FLAGS_sandbox2_monitor_ptrace_use_deadline_manager);
}

void PtraceMonitor::SetStackTraceResultInfo(const Regs* regs) {
  auto stack_trace = GetAndLogStackTrace(regs);
  if (!stack_trace.ok()) {
    LOG_IF(ERROR,
           absl::GetFlag(FLAGS_sandbox2_log_unobtainable_stack_traces_errors))
        << "Could not obtain stack trace: " << stack_trace.status();
    return;
  }
  result_.set_stack_trace(*stack_trace);
}

void PtraceMonitor::SetAllThreadsStackTraceResultInfo(const Regs* regs) {
  absl::StatusOr<absl::flat_hash_set<int>> tasks =
      sanitizer::GetListOfTasks(regs->pid());
  if (!tasks.ok()) {
    LOG(ERROR) << "Could not list tasks: " << tasks.status();
    return;
  }

  // Remove the current thread from the list, we do not need to interrupt it.
  tasks->erase(regs->pid());

  // Interrupt all tasks. If a task cannot be interrupted, we just filter it out
  // and collect stack traces on a best-effort basis.
  // Also, note that we do not want to continue those tasks after fetching the
  // stack traces because this will be handled by the main waiting loop, which
  // also knows how to handle tasks being PTRACE_INTERRUPT-ed.
  absl::erase_if(*tasks, [](int task) {
    if (ptrace(PTRACE_INTERRUPT, task, 0, 0) != 0) {
      LOG(ERROR) << "Could not interrupt task: " << task;
      return true;
    }
    return false;
  });

  // Similarly, if we fail at waiting for a task to stop, we just filter it out.
  std::vector<pid_t> fetch_tasks;
  fetch_tasks.reserve(tasks->size() + 1);
  fetch_tasks.push_back(regs->pid());
  auto deadline = absl::Now() + absl::Milliseconds(500);
  for (pid_t task : *tasks) {
    if (absl::Now() >= deadline) {
      LOG(ERROR) << "Could not wait for tasks to stop in time, skipping "
                    "remaining tasks";
      break;
    }
    // We wait for the task to stop here, but the actual task stops will be
    // handled by the main waiting loop.
    if (WaitForTaskToStop(task, absl::Milliseconds(10)).ok()) {
      fetch_tasks.push_back(task);
    }
  }

  if (!tasks.ok()) {
    LOG(ERROR) << "Could not list tasks: " << tasks.status();
    return;
  }

  std::vector<std::pair<pid_t, std::vector<std::string>>> thread_stack_traces;
  for (pid_t task : fetch_tasks) {
    auto stack_trace = GetAndLogStackTraceOfPid(task);
    if (!stack_trace.ok()) {
      LOG_IF(ERROR,
             absl::GetFlag(FLAGS_sandbox2_log_unobtainable_stack_traces_errors))
          << "Could not obtain stack trace: " << stack_trace.status();
      continue;
    }
    if (task == regs->pid()) {
      result_.set_stack_trace(*stack_trace);
    }
    thread_stack_traces.push_back({task, *std::move(stack_trace)});
  }

  result_.set_thread_stack_trace(std::move(thread_stack_traces));
}

void PtraceMonitor::SetAdditionalResultInfo(std::unique_ptr<Regs> regs) {
  pid_t pid = regs->pid();
  result_.SetRegs(std::move(regs));
  result_.SetProgName(util::GetProgName(pid));
  result_.SetProcMaps(ReadProcMaps(pid));
  if (!ShouldCollectStackTrace(result_.final_status())) {
    VLOG(1) << "Stack traces have been disabled";
    return;
  }

  if (policy_->collect_all_threads_stacktrace()) {
    SetAllThreadsStackTraceResultInfo(result_.GetRegs());
  } else {
    SetStackTraceResultInfo(result_.GetRegs());
  }
}

bool PtraceMonitor::KillSandboxee() {
  VLOG(1) << "Sending SIGKILL to the PID: " << process_.main_pid;
  if (kill(process_.main_pid, SIGKILL) != 0) {
    PLOG(ERROR) << "Could not send SIGKILL to PID " << process_.main_pid;
    SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_KILL);
    return false;
  }
  if (hard_deadline_ == absl::InfiniteFuture()) {
    hard_deadline_ =
        absl::Now() +
        absl::GetFlag(FLAGS_sandbox2_monitor_ptrace_graceful_kill_timeout);
  }
  return true;
}

bool PtraceMonitor::InterruptSandboxee() {
  if (ptrace(PTRACE_INTERRUPT, process_.main_pid, 0, 0) == -1) {
    PLOG(ERROR) << "Could not send interrupt to pid=" << process_.main_pid;
    SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_INTERRUPT);
    return false;
  }
  return true;
}

// Not defined in glibc.
#define __WPTRACEEVENT(x) ((x & 0xff0000) >> 16)

void PtraceMonitor::NotifyMonitor() {
  if (use_deadline_manager_) {
    pid_waiter_.Notify();
  } else {
    absl::MutexLock lock(&thread_mutex_);
    if (thread_.IsJoinable()) {
      pthread_kill(thread_.handle(), SIGCHLD);
    }
  }
}

void PtraceMonitor::Join() {
  absl::MutexLock lock(&thread_mutex_);
  if (thread_.IsJoinable()) {
    thread_.Join();
    CHECK(IsDone()) << "Monitor did not terminate";
    VLOG(1) << "Final execution status: " << result_.ToString();
    CHECK(result_.final_status() != Result::UNSET);
  }
}

void PtraceMonitor::RunInternal() {
  {
    absl::MutexLock lock(&thread_mutex_);
    thread_ = sapi::Thread(this, &PtraceMonitor::Run, "sandbox2-Monitor");
  }

  // Wait for the Monitor to set-up the sandboxee correctly (or fail while
  // doing that). From here on, it is safe to use the IPC object for
  // non-sandbox-related data exchange.
  setup_notification_.WaitForNotification();
}

void PtraceMonitor::Run() {
  absl::Cleanup monitor_done = [this] {
    getrusage(RUSAGE_THREAD, result_.GetRUsageMonitor());
    OnDone();
  };

  absl::Cleanup setup_notify = [this] { setup_notification_.Notify(); };
  // It'd be costly to initialize the sigset_t for each sigtimedwait()
  // invocation, so do it once per Monitor.
  if (!use_deadline_manager_ && !InitSetupSignals()) {
    SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_SIGNALS);
    return;
  }
  // This call should be the last in the init sequence, because it can cause the
  // sandboxee to enter ptrace-stopped state, in which it will not be able to
  // send any messages over the Comms channel.
  if (!InitPtraceAttach()) {
    SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_PTRACE);
    return;
  }

  // Tell the parent thread (Sandbox2 object) that we're done with the initial
  // set-up process of the sandboxee.
  std::move(setup_notify).Invoke();

  bool sandboxee_exited = false;
  pid_waiter_.SetPriorityPid(process_.main_pid);
  int status;
  struct rusage rusage;
  // All possible still running children of main process, will be killed due to
  // PTRACE_O_EXITKILL ptrace() flag.
  while (result().final_status() == Result::UNSET) {
    if (absl::Now() >= hard_deadline_) {
      LOG(WARNING) << "Hard deadline exceeded (timed_out=" << timed_out_
                   << ", external_kill=" << external_kill_
                   << ", network_violation=" << network_violation_ << ").";
      SetExitStatusCode(Result::TIMEOUT, 0);
      break;
    }
    int64_t deadline = deadline_millis_.load(std::memory_order_relaxed);
    if (deadline != 0 && absl::Now() >= absl::FromUnixMillis(deadline)) {
      VLOG(1) << "Sandbox process hit timeout due to the walltime timer";
      timed_out_ = true;
      if (!KillSandboxee()) {
        break;
      }
    }

    if (!dump_stack_request_flag_.test_and_set(std::memory_order_relaxed)) {
      should_dump_stack_ = true;
      if (!InterruptSandboxee()) {
        break;
      }
    }

    if (!external_kill_request_flag_.test_and_set(std::memory_order_relaxed)) {
      external_kill_ = true;
      if (!KillSandboxee()) {
        break;
      }
    }

    if (network_proxy_server_ &&
        network_proxy_server_->violation_occurred_.load(
            std::memory_order_acquire) &&
        !network_violation_) {
      network_violation_ = true;
      if (!KillSandboxee()) {
        break;
      }
    }
    if (use_deadline_manager_) {
      absl::Time effective_deadline = hard_deadline_;
      if (deadline != 0 && hard_deadline_ == absl::InfiniteFuture()) {
        effective_deadline = absl::FromUnixMillis(deadline);
      }
      pid_waiter_.SetDeadline(effective_deadline);
    }
    pid_t ret = pid_waiter_.Wait(&status, &rusage);
    if (ret == 0) {
      if (!use_deadline_manager_) {
        constexpr timespec ts = {kWakeUpPeriodSec, kWakeUpPeriodNSec};
        int signo = sigtimedwait(&sset_, nullptr, &ts);
        LOG_IF(ERROR, signo != -1 && signo != SIGCHLD)
            << "Unknown signal received: " << signo;
      }
      continue;
    }

    if (ret == -1) {
      if (errno == ECHILD) {
        LOG(ERROR) << "PANIC(). The main process has not exited yet, "
                   << "yet we haven't seen its exit event";
        SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_CHILD);
      } else if (!use_deadline_manager_ || errno != EINTR) {
        PLOG(ERROR) << "waitpid() failed";
      }
      continue;
    }

    VLOG(3) << "waitpid() returned with PID: " << ret << ", status: " << status;

    if (WIFEXITED(status)) {
      VLOG(1) << "PID: " << ret
              << " finished with code: " << WEXITSTATUS(status);
      // That's the main process, set the exit code, and exit. It will kill
      // all remaining processes (if there are any) because of the
      // PTRACE_O_EXITKILL ptrace() flag.
      if (ret == process_.main_pid) {
        if (!wait_for_execveat()) {
          SetExitStatusCode(Result::OK, WEXITSTATUS(status));
          result_.SetRUsageSandboxee(rusage);
        } else {
          SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_MONITOR);
        }
        sandboxee_exited = true;
      }
    } else if (WIFSIGNALED(status)) {
      //  This usually does not happen, but might.
      //  Quote from the manual:
      //   A SIGKILL signal may still cause a PTRACE_EVENT_EXIT stop before
      //   actual signal death.  This may be changed in the future;
      VLOG(1) << "PID: " << ret << " terminated with signal: "
              << util::GetSignalName(WTERMSIG(status));
      if (ret == process_.main_pid) {
        result_.SetRUsageSandboxee(rusage);
        if (network_violation_) {
          SetExitStatusCode(Result::VIOLATION, Result::VIOLATION_NETWORK);
          result_.SetNetworkViolation(network_proxy_server_->violation_msg_);
        } else if (external_kill_) {
          SetExitStatusCode(Result::EXTERNAL_KILL, 0);
        } else if (timed_out_) {
          SetExitStatusCode(Result::TIMEOUT, 0);
        } else {
          SetExitStatusCode(Result::SIGNALED, WTERMSIG(status));
        }
        sandboxee_exited = true;
      }
    } else if (WIFSTOPPED(status)) {
      VLOG(2) << "PID: " << ret
              << " received signal: " << util::GetSignalName(WSTOPSIG(status))
              << " with event: "
              << util::GetPtraceEventName(__WPTRACEEVENT(status));
      StateProcessStopped(ret, status);
    } else if (WIFCONTINUED(status)) {
      VLOG(2) << "PID: " << ret << " is being continued";
    }
  }

  if (!sandboxee_exited) {
    const bool log_stack_traces =
        result_.final_status() != Result::OK &&
        absl::GetFlag(FLAGS_sandbox2_log_all_stack_traces);
    constexpr auto kGracefullExitTimeout = absl::Milliseconds(200);
    auto deadline = absl::Now() + kGracefullExitTimeout;
    if (log_stack_traces) {
      deadline = absl::Now() +
                 absl::GetFlag(FLAGS_sandbox2_stack_traces_collection_timeout);
    }
    for (;;) {
      auto left = deadline - absl::Now();
      if (absl::Now() >= deadline) {
        LOG(WARNING)
            << "Waiting for sandboxee exit timed out. Sandboxee result: "
            << result_.ToString();
        break;
      }
      if (use_deadline_manager_) {
        pid_waiter_.SetDeadline(deadline);
      }
      pid_t ret = pid_waiter_.Wait(&status, &rusage);
      if (ret == -1) {
        if (use_deadline_manager_ && errno == EINTR) {
          continue;
        }
        if (!log_stack_traces || ret != ECHILD) {
          PLOG(ERROR) << "waitpid() failed";
        }
        break;
      }
      if (ret == process_.main_pid &&
          (WIFSIGNALED(status) || WIFEXITED(status))) {
        result_.SetRUsageSandboxee(rusage);
        if (!log_stack_traces) {
          break;
        }
      }
      if (!log_stack_traces) {
        kill(process_.main_pid, SIGKILL);
      }

      if (ret == 0) {
        if (!use_deadline_manager_) {
          auto ts = absl::ToTimespec(left);
          sigtimedwait(&sset_, nullptr, &ts);
        }
        continue;
      }

      if (WIFSTOPPED(status)) {
        if (log_stack_traces) {
          GetAndLogStackTraceOfPid(ret).IgnoreError();
        }

        if (__WPTRACEEVENT(status) == PTRACE_EVENT_EXIT) {
          VLOG(2) << "PID: " << ret << " PTRACE_EVENT_EXIT ";
          ContinueProcess(ret, 0);
          continue;
        }
      }
    }
  }
}

absl::StatusOr<std::vector<std::string>>
PtraceMonitor::GetAndLogStackTraceOfPid(pid_t pid) {
  if (!StackTraceCollectionPossible()) {
    return absl::InternalError("Stack trace collection is not possible");
  }

  Regs regs(pid);
  if (auto status = regs.Fetch(); !status.ok()) {
    LOG(ERROR) << "Failed to get regs, PID:" << pid << " status:" << status;
    return absl::InternalError(absl::StrCat("Failed to get regs, PID:", pid,
                                            " status:", status.message()));
  }

  auto stack_trace = GetAndLogStackTrace(&regs);
  if (!stack_trace.ok()) {
    LOG(ERROR) << "Failed to get stack trace, PID:" << pid
               << " status:" << stack_trace.status();
  }
  return stack_trace;
}

bool PtraceMonitor::InitSetupSignals() {
  if (sigemptyset(&sset_) == -1) {
    PLOG(ERROR) << "sigemptyset()";
    return false;
  }

  // sigtimedwait will react (wake-up) to arrival of this signal.
  if (sigaddset(&sset_, SIGCHLD) == -1) {
    PLOG(ERROR) << "sigaddset(SIGCHLD)";
    return false;
  }

  if (pthread_sigmask(SIG_BLOCK, &sset_, nullptr) == -1) {
    PLOG(ERROR) << "pthread_sigmask(SIG_BLOCK, SIGCHLD)";
    return false;
  }

  return true;
}

absl::Status TryAttach(const absl::flat_hash_set<int>& tasks,
                       absl::Time deadline,
                       absl::flat_hash_set<int>& tasks_attached) {
  constexpr intptr_t kPtraceOptions =
      PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK |
      PTRACE_O_TRACEVFORKDONE | PTRACE_O_TRACECLONE | PTRACE_O_TRACEEXEC |
      PTRACE_O_TRACEEXIT | PTRACE_O_TRACESECCOMP | PTRACE_O_EXITKILL;
  auto format_ptrace_error = [](int task, absl::string_view message) {
    return absl::StrCat("ptrace(PTRACE_SEIZE, ", task, ", 0, ", "0x",
                        absl::Hex(kPtraceOptions), "): ", message);
  };

  absl::flat_hash_set<int> cur_tasks = tasks;
  int retries = 0;

  // In some situations we allow ptrace to try again when it fails.
  while (!cur_tasks.empty()) {
    absl::flat_hash_set<int> retry_tasks;
    for (int task : cur_tasks) {
      if (tasks_attached.contains(task)) {
        continue;
      }
      int ret = ptrace(PTRACE_SEIZE, task, 0, kPtraceOptions);
      if (ret != 0) {
        if (errno == EPERM) {
          // Sometimes when a task is exiting we can get an EPERM from ptrace.
          // Let's try again up until the timeout in this situation.
          PLOG(WARNING) << format_ptrace_error(task, "Retrying after EPERM");
          retry_tasks.insert(task);
          continue;
        }
        if (errno == ESRCH) {
          // A task may have exited since we captured the task list, we will
          // allow things to continue after we log a warning.
          PLOG(WARNING) << format_ptrace_error(
              task, "Skipping exited task. Continuing with other tasks.");
          continue;
        }
        // Any other errno will be considered a failure.
        return absl::ErrnoToStatus(errno, format_ptrace_error(task, "Failure"));
      }
      tasks_attached.insert(task);
    }
    if (!retry_tasks.empty()) {
      if (absl::Now() >= deadline) {
        return absl::DeadlineExceededError(absl::StrCat(
            "Attaching to sandboxee timed out: could not attach to ",
            cur_tasks.size(), " tasks"));
      }
      // Exponential Backoff.
      constexpr absl::Duration kInitialRetry = absl::Milliseconds(1);
      constexpr absl::Duration kMaxRetry = absl::Milliseconds(20);
      const absl::Duration retry_interval =
          kInitialRetry * (1 << std::min(10, retries++));
      absl::SleepFor(
          std::min({retry_interval, kMaxRetry, deadline - absl::Now()}));
    }
    cur_tasks = std::move(retry_tasks);
  }

  return absl::OkStatus();
}

bool PtraceMonitor::InitPtraceAttach() {
  if (process_.init_pid > 0) {
    if (ptrace(PTRACE_SEIZE, process_.init_pid, 0, PTRACE_O_EXITKILL) != 0) {
      if (errno != ESRCH) {
        PLOG(ERROR) << "attaching to init process failed";
      }
      return false;
    }
  }

  // Get a list of tasks.
  absl::StatusOr<absl::flat_hash_set<int>> tasks =
      sanitizer::GetListOfTasks(process_.main_pid);
  if (!tasks.ok()) {
    LOG(ERROR) << "Could not get list of tasks: " << tasks.status().message();
    return false;
  }

  if (!tasks->contains(process_.main_pid)) {
    LOG(ERROR) << "The pid " << process_.main_pid
               << " was not found in its own tasklist.";
    return false;
  }

  // With TSYNC, we can allow threads: seccomp applies to all threads.
  if (tasks->size() > 1) {
    LOG(WARNING) << "PID " << process_.main_pid << " has " << tasks->size()
                 << " threads, at the time of call to SandboxMeHere(). If you "
                    "are seeing more sandbox violations than expected, this "
                    "might be the reason why"
                 << ".";
  }

  absl::flat_hash_set<int> tasks_attached;
  absl::Time deadline = absl::Now() + absl::Seconds(4);

  constexpr int kMaxRetries = 3;
  for (int retries = 0; retries < kMaxRetries && *tasks != tasks_attached;
       ++retries) {
    if (retries > 0) {
      LOG(ERROR) << "PID " << process_.main_pid
                 << " spawned new threads while we were trying to attach to it "
                    "(attempt "
                 << retries << "/" << kMaxRetries << ")";
    }
    if (absl::Status status = TryAttach(*tasks, deadline, tasks_attached);
        !status.ok()) {
      LOG(ERROR) << status.message();
      return false;
    }

    // Get a list of tasks after attaching.
    tasks = sanitizer::GetListOfTasks(process_.main_pid);
    if (!tasks.ok()) {
      LOG(ERROR) << "Could not get list of tasks: " << tasks.status().message();
      return false;
    }
  }
  if (*tasks != tasks_attached) {
    LOG(ERROR) << "PID " << process_.main_pid
               << " spawned new threads while we were trying to attach to it "
                  "(retries exhausted)";
    return false;
  }

  // No glibc wrapper for gettid - see 'man gettid'.
  VLOG(1) << "Monitor (PID: " << getpid()
          << ", TID: " << util::Syscall(__NR_gettid)
          << ") attached to PID: " << process_.main_pid;

  // Technically, the sandboxee can be in a ptrace-stopped state right now,
  // because some signal could have arrived in the meantime. Yet, this
  // Comms::SendUint32 call shouldn't lock our process, because the underlying
  // socketpair() channel is buffered, hence it will accept the uint32_t message
  // no matter what is the current state of the sandboxee, and it will allow for
  // our process to continue and unlock the sandboxee with the proper ptrace
  // event handling.
  if (!SendMonitorReadyMessageAndFlags(Client::kSandbox2ClientPtrace)) {
    LOG(ERROR) << "Couldn't send Client::kSandbox2ClientPtrace message";
    return false;
  }
  return true;
}

void PtraceMonitor::ActionProcessSyscall(Regs* regs, const Syscall& syscall) {
  // If the sandboxing is not enabled yet, allow the first __NR_execveat.
  if (syscall.nr() == __NR_execveat && wait_for_execveat()) {
    VLOG(1) << "[PERMITTED/BEFORE_EXECVEAT]: " << "SYSCALL ::: PID: "
            << regs->pid() << ", PROG: '" << util::GetProgName(regs->pid())
            << "' : " << syscall.GetDescription();
    ContinueProcess(regs->pid(), 0);
    return;
  }

  // Notify can decide whether we want to allow this syscall. It could be useful
  // for sandbox setups in which some syscalls might still need some logging,
  // but nonetheless be allowed ('permissible syscalls' in sandbox v1).
  auto trace_response = notify_->EventSyscallTrace(syscall);
  if (trace_response == Notify::TraceAction::kAllow) {
    ContinueProcess(regs->pid(), 0);
    return;
  }
  if (trace_response == Notify::TraceAction::kInspectAfterReturn) {
    // Note that a process might die without an exit-stop before the syscall is
    // completed (eg. a thread calls execve() and the thread group leader dies),
    // so the entry is removed when the process exits.
    syscalls_in_progress_[regs->pid()] = syscall;
    CompleteSyscall(regs->pid(), 0);
    return;
  }

  if (absl::GetFlag(FLAGS_sandbox2_danger_danger_permit_all) || log_file_) {
    std::string syscall_description = syscall.GetDescription();
    if (log_file_) {
      PCHECK(absl::FPrintF(log_file_, "PID: %d %s\n", regs->pid(),
                           syscall_description) >= 0);
    }
    VLOG(1) << "PID: " << regs->pid() << " " << syscall_description;
    ContinueProcess(regs->pid(), 0);
    return;
  }

  ActionProcessSyscallViolation(regs, syscall, ViolationType::kSyscall);
}

void PtraceMonitor::ActionProcessSyscallViolation(
    Regs* regs, const Syscall& syscall, ViolationType violation_type) {
  LogSyscallViolation(syscall);
  notify_->EventSyscallViolation(syscall, violation_type);
  SetExitStatusCode(Result::VIOLATION, syscall.nr());
  result_.SetSyscall(std::make_unique<Syscall>(syscall));
  SetAdditionalResultInfo(std::make_unique<Regs>(*regs));
  // Rewrite the syscall argument to something invalid (-1).
  // The process will be killed anyway so this is just a precaution.
  auto status = regs->SkipSyscallReturnValue(-ENOSYS);
  if (!status.ok()) {
    LOG(ERROR) << status;
  }
}

void PtraceMonitor::EventPtraceSeccomp(pid_t pid, int event_msg) {
  if (event_msg < sapi::cpu::Architecture::kUnknown ||
      event_msg > sapi::cpu::Architecture::kMax) {
    // We've observed that, if the process has exited, the event_msg may contain
    // the exit status even though we haven't received the exit event yet.
    // To work around this, if the event msg is not in the range of the known
    // architectures, we assume that it's an exit status. We deal with it by
    // ignoring this event, and we'll get the exit event in the next iteration.
    LOG(WARNING) << "received event_msg for unknown architecture: " << event_msg
                 << "; the program may have exited";
    return;
  }

  // If the seccomp-policy is using RET_TRACE, we request that it returns the
  // syscall architecture identifier in the SECCOMP_RET_DATA.
  const auto syscall_arch = static_cast<sapi::cpu::Architecture>(event_msg);
  Regs regs(pid);
  auto status = regs.Fetch();
  if (!status.ok()) {
    // Ignore if process is killed in the meanwhile
    if (absl::IsNotFound(status)) {
      LOG(WARNING) << "failed to fetch regs: " << status;
      return;
    }
    LOG(ERROR) << "failed to fetch regs: " << status;
    SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_FETCH);
    return;
  }

  Syscall syscall = regs.ToSyscall(syscall_arch);
  // If the architecture of the syscall used is different that the current host
  // architecture, report a violation.
  if (syscall_arch != Syscall::GetHostArch()) {
    ActionProcessSyscallViolation(&regs, syscall,
                                  ViolationType::kArchitectureSwitch);
    return;
  }

  ActionProcessSyscall(&regs, syscall);
}

void PtraceMonitor::EventSyscallExit(pid_t pid) {
  // Check that the monitor wants to inspect the current syscall's return value.
  auto index = syscalls_in_progress_.find(pid);
  if (index == syscalls_in_progress_.end()) {
    LOG(ERROR) << "Expected a syscall in progress in PID " << pid;
    SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_INSPECT);
    return;
  }
  Regs regs(pid);
  auto status = regs.Fetch();
  if (!status.ok()) {
    // Ignore if process is killed in the meanwhile
    if (absl::IsNotFound(status)) {
      LOG(WARNING) << "failed to fetch regs: " << status;
      return;
    }
    LOG(ERROR) << "failed to fetch regs: " << status;
    SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_FETCH);
    return;
  }
  int64_t return_value = regs.GetReturnValue(sapi::host_cpu::Architecture());
  notify_->EventSyscallReturn(index->second, return_value);
  syscalls_in_progress_.erase(index);
  ContinueProcess(pid, 0);
}

void PtraceMonitor::EventPtraceNewProcess(pid_t pid, int event_msg) {
  // ptrace doesn't issue syscall-exit-stops for successful fork/vfork/clone
  // system calls. Check if the monitor wanted to inspect the syscall's return
  // value, and call EventSyscallReturn for the parent process if so.
  auto index = syscalls_in_progress_.find(pid);
  if (index != syscalls_in_progress_.end()) {
    auto syscall_nr = index->second.nr();
    bool creating_new_process = syscall_nr == __NR_clone;
#ifdef __NR_clone3
    creating_new_process = creating_new_process || syscall_nr == __NR_clone3;
#endif
#ifdef __NR_fork
    creating_new_process = creating_new_process || syscall_nr == __NR_fork;
#endif
#ifdef __NR_vfork
    creating_new_process = creating_new_process || syscall_nr == __NR_vfork;
#endif
    if (!creating_new_process) {
      LOG(ERROR) << "Expected a fork/vfork/clone syscall in progress in PID "
                 << pid << "; actual: " << index->second.GetDescription();
      SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_INSPECT);
      return;
    }
    notify_->EventSyscallReturn(index->second, event_msg);
    syscalls_in_progress_.erase(index);
  }
  ContinueProcess(pid, 0);
}

void PtraceMonitor::EventPtraceExec(pid_t pid, int event_msg) {
  if (wait_for_execveat()) {
    VLOG(1) << "PTRACE_EVENT_EXEC seen from PID: " << event_msg
            << ". SANDBOX ENABLED!";
    set_wait_for_execveat(false);
  } else {
    // ptrace doesn't issue syscall-exit-stops for successful execve/execveat
    // system calls. Check if the monitor wanted to inspect the syscall's return
    // value, and call EventSyscallReturn if so.
    auto index = syscalls_in_progress_.find(pid);
    if (index != syscalls_in_progress_.end()) {
      auto syscall_nr = index->second.nr();
      if (syscall_nr != __NR_execve && syscall_nr != __NR_execveat) {
        LOG(ERROR) << "Expected an execve/execveat syscall in progress in PID "
                   << pid << "; actual: " << index->second.GetDescription();
        SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_INSPECT);
        return;
      }
      notify_->EventSyscallReturn(index->second, 0);
      syscalls_in_progress_.erase(index);
    }
  }
  ContinueProcess(pid, 0);
}

void PtraceMonitor::EventPtraceExit(pid_t pid, int event_msg) {
  // Forget about any syscalls in progress for this PID.
  syscalls_in_progress_.erase(pid);

  // A regular exit, let it continue (fast-path).
  if (ABSL_PREDICT_TRUE(WIFEXITED(event_msg) &&
                        (!policy_->collect_stacktrace_on_exit() ||
                         pid != process_.main_pid))) {
    ContinueProcess(pid, 0);
    return;
  }

  const bool is_seccomp =
      WIFSIGNALED(event_msg) && WTERMSIG(event_msg) == SIGSYS;
  const bool log_stack_trace =
      absl::GetFlag(FLAGS_sandbox2_log_all_stack_traces);
  // Fetch the registers as we'll need them to fill the result in any case
  auto regs = std::make_unique<Regs>(pid);
  if (is_seccomp || pid == process_.main_pid || log_stack_trace) {
    auto status = regs->Fetch();
    if (!status.ok()) {
      LOG(ERROR) << "failed to fetch regs: " << status;
      SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_FETCH);
      return;
    }
  }

  // Process signaled due to seccomp violation.
  if (is_seccomp) {
    VLOG(1) << "PID: " << pid << " violation uncovered via the EXIT_EVENT";
    ActionProcessSyscallViolation(regs.get(),
                                  regs->ToSyscall(Syscall::GetHostArch()),
                                  ViolationType::kSyscall);
    return;
  }

  // This can be reached in four cases:
  // 1) Process was killed from the sandbox.
  // 2) Process was killed because it hit a timeout.
  // 3) Regular signal/other exit cause.
  // 4) Normal exit for which we want to obtain stack trace.
  if (pid == process_.main_pid) {
    VLOG(1) << "PID: " << pid << " main special exit";
    if (network_violation_) {
      SetExitStatusCode(Result::VIOLATION, Result::VIOLATION_NETWORK);
      result_.SetNetworkViolation(network_proxy_server_->violation_msg_);
    } else if (external_kill_) {
      SetExitStatusCode(Result::EXTERNAL_KILL, 0);
    } else if (timed_out_) {
      SetExitStatusCode(Result::TIMEOUT, 0);
    } else if (WIFEXITED(event_msg)) {
      SetExitStatusCode(Result::OK, WEXITSTATUS(event_msg));
    } else {
      SetExitStatusCode(Result::SIGNALED, WTERMSIG(event_msg));
    }
    SetAdditionalResultInfo(std::move(regs));
  } else if (log_stack_trace) {
    // In case pid == pid_ the stack trace will be logged anyway. So we need
    // to do explicit logging only when this is not a main PID.
    if (StackTraceCollectionPossible()) {
      if (auto stack_trace = GetAndLogStackTrace(regs.get());
          !stack_trace.ok()) {
        LOG(ERROR) << "Failed to get stack trace, PID:" << pid
                   << " status:" << stack_trace.status();
      }
    }
  }
  VLOG(1) << "Continuing";
  ContinueProcess(pid, 0);
}

void PtraceMonitor::EventPtraceStop(pid_t pid, int stopsig) {
  // It's not a real stop signal. For example PTRACE_O_TRACECLONE and similar
  // flags to ptrace(PTRACE_SEIZE) might generate this event with SIGTRAP.
  if (stopsig != SIGSTOP && stopsig != SIGTSTP && stopsig != SIGTTIN &&
      stopsig != SIGTTOU) {
    ContinueProcess(pid, 0);
    return;
  }
  // It's our PID stop signal. Stop it.
  VLOG(2) << "PID: " << pid << " stopped due to "
          << util::GetSignalName(stopsig);
  StopProcess(pid, 0);
}

void PtraceMonitor::StateProcessStopped(pid_t pid, int status) {
  int stopsig = WSTOPSIG(status);
  // We use PTRACE_O_TRACESYSGOOD, so we can tell it's a syscall stop without
  // calling PTRACE_GETSIGINFO by checking the value of the reported signal.
  bool is_syscall_exit = stopsig == (SIGTRAP | 0x80);
  if (__WPTRACEEVENT(status) == 0 && !is_syscall_exit) {
    // Must be a regular signal delivery.
    VLOG(2) << "PID: " << pid
            << " received signal: " << util::GetSignalName(stopsig);
    notify_->EventSignal(pid, stopsig);
    ContinueProcess(pid, stopsig);
    return;
  }

  unsigned long event_msg;  // NOLINT
  if (ptrace(PTRACE_GETEVENTMSG, pid, 0, &event_msg) == -1) {
    if (errno == ESRCH) {
      // This happens from time to time, the kernel does not guarantee us that
      // we get the event in time.
      PLOG(INFO) << "ptrace(PTRACE_GETEVENTMSG, " << pid << ")";
      return;
    }
    PLOG(ERROR) << "ptrace(PTRACE_GETEVENTMSG, " << pid << ")";
    SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_GETEVENT);
    return;
  }

  if (ABSL_PREDICT_FALSE(pid == process_.main_pid && should_dump_stack_ &&
                         policy_->GetNamespace())) {
    auto stack_trace = [this,
                        pid]() -> absl::StatusOr<std::vector<std::string>> {
      Regs regs(pid);
      SAPI_RETURN_IF_ERROR(regs.Fetch());
      return GetStackTrace(&regs);
    }();

    if (!stack_trace.ok()) {
      LOG(WARNING) << "FAILED TO GET SANDBOX STACK : " << stack_trace.status();
    } else if (VLOG_IS_ON(0)) {
      VLOG(0) << "SANDBOX STACK: PID: " << pid << ", [";
      for (const auto& frame : *stack_trace) {
        VLOG(0) << "  " << frame;
      }
      VLOG(0) << "]";
    }
    should_dump_stack_ = false;
  }

  if (is_syscall_exit) {
    VLOG(2) << "PID: " << pid << " syscall-exit-stop: " << event_msg;
    EventSyscallExit(pid);
    return;
  }

  switch (__WPTRACEEVENT(status)) {
    case PTRACE_EVENT_FORK:
      VLOG(2) << "PID: " << pid << " PTRACE_EVENT_FORK, PID: " << event_msg;
      EventPtraceNewProcess(pid, event_msg);
      break;
    case PTRACE_EVENT_VFORK:
      VLOG(2) << "PID: " << pid << " PTRACE_EVENT_VFORK, PID: " << event_msg;
      EventPtraceNewProcess(pid, event_msg);
      break;
    case PTRACE_EVENT_CLONE:
      VLOG(2) << "PID: " << pid << " PTRACE_EVENT_CLONE, PID: " << event_msg;
      EventPtraceNewProcess(pid, event_msg);
      break;
    case PTRACE_EVENT_VFORK_DONE:
      ContinueProcess(pid, 0);
      break;
    case PTRACE_EVENT_EXEC:
      VLOG(2) << "PID: " << pid << " PTRACE_EVENT_EXEC, PID: " << event_msg;
      EventPtraceExec(pid, event_msg);
      break;
    case PTRACE_EVENT_EXIT:
      VLOG(2) << "PID: " << pid << " PTRACE_EVENT_EXIT: " << event_msg;
      EventPtraceExit(pid, event_msg);
      break;
    case PTRACE_EVENT_STOP:
      VLOG(2) << "PID: " << pid << " PTRACE_EVENT_STOP: " << event_msg;
      EventPtraceStop(pid, stopsig);
      break;
    case PTRACE_EVENT_SECCOMP:
      VLOG(2) << "PID: " << pid << " PTRACE_EVENT_SECCOMP: " << event_msg;
      EventPtraceSeccomp(pid, event_msg);
      break;
    default:
      LOG(ERROR) << "Unknown ptrace event: " << __WPTRACEEVENT(status)
                 << " with data: " << event_msg;
      break;
  }
}

}  // namespace sandbox2
