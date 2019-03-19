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

// Implementation file for the sandbox2::Monitor class.

#include "sandboxed_api/sandbox2/monitor.h"

#include <linux/posix_types.h>  // NOLINT: Needs to come before linux/ipc.h

#include <linux/ipc.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>

#include <glog/logging.h>
#include "sandboxed_api/util/flag.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/limits.h"
#include "sandboxed_api/sandbox2/mounts.h"
#include "sandboxed_api/sandbox2/namespace.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/regs.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sanitizer.h"
#include "sandboxed_api/sandbox2/stack-trace.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/sandbox2/util.h"

ABSL_FLAG(bool, sandbox2_report_on_sandboxee_signal, true,
          "Report sandbox2 sandboxee deaths caused by signals");

ABSL_FLAG(bool, sandbox2_report_on_sandboxee_timeout, true,
          "Report sandbox2 sandboxee timeouts");

ABSL_DECLARE_FLAG(bool, sandbox2_danger_danger_permit_all);
ABSL_DECLARE_FLAG(string, sandbox2_danger_danger_permit_all_and_log);

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

}  // namespace

Monitor::Monitor(Executor* executor, Policy* policy, Notify* notify)
    : executor_(executor),
      notify_(notify),
      policy_(policy),
      comms_(executor_->ipc()->comms()),
      ipc_(executor_->ipc()),
      setup_counter_(new absl::BlockingCounter(1)),
      done_(false),
      wait_for_execve_(executor->enable_sandboxing_pre_execve_) {
  std::string path = absl::GetFlag(FLAGS_sandbox2_danger_danger_permit_all_and_log);
  if (!path.empty()) {
    log_file_ = std::fopen(path.c_str(), "a+");
    PCHECK(log_file_ != nullptr) << "Failed to open log file '" << path << "'";
  }
}

Monitor::~Monitor() {
  CleanUpTimer();
  if (log_file_) {
    std::fclose(log_file_);
  }
}

void Monitor::Run() {
  using DecrementCounter = decltype(setup_counter_);
  std::unique_ptr<DecrementCounter, std::function<void(DecrementCounter*)>>
      decrement_count{&setup_counter_, [](DecrementCounter* counter) {
                        (*counter)->DecrementCount();
                      }};

  struct MonitorCleanup {
    ~MonitorCleanup() {
      getrusage(RUSAGE_THREAD, capture->result_.GetRUsageMonitor());
      capture->notify_->EventFinished(capture->result_);
      capture->ipc_->InternalCleanupFdMap();
      absl::MutexLock lock(&capture->done_mutex_);
      capture->done_.store(true, std::memory_order_release);
    }
    Monitor* capture;
  } monitor_cleanup{this};

  if (!InitSetupTimer()) {
    result_.SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_TIMERS);
    return;
  }

  // It'd be costly to initialize the sigset_t for each sigtimedwait()
  // invocation, so do it once per Monitor.
  sigset_t sigtimedwait_sset;
  if (!InitSetupSignals(&sigtimedwait_sset)) {
    result_.SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_SIGNALS);
    return;
  }

  // Don't trace the child: it will allow to use 'strace -f' with the whole
  // sandbox master/monitor, which ptrace_attach'es to the child.
  int clone_flags = CLONE_UNTRACED;

  // Get PID of the sandboxee.
  pid_t init_pid = 0;
  pid_ = executor_->StartSubProcess(clone_flags, policy_->GetNamespace(),
                                    policy_->GetCapabilities(), &init_pid);

  if (init_pid < 0) {
    // TODO(hamacher): does this require additional handling here?
    LOG(ERROR) << "Spawning init process failed";
  } else if (init_pid > 0) {
    PCHECK(ptrace(PTRACE_SEIZE, init_pid, 0, PTRACE_O_EXITKILL) == 0);
  }

  if (pid_ < 0) {
    result_.SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_SUBPROCESS);
    return;
  }

  if (!notify_->EventStarted(pid_, comms_)) {
    result_.SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_NOTIFY);
    return;
  }
  if (!InitAcceptConnection()) {
    result_.SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_CONNECTION);
    return;
  }
  if (!InitSendIPC()) {
    result_.SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_IPC);
    return;
  }
  if (!InitSendCwd()) {
    result_.SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_CWD);
    return;
  }
  if (!InitSendPolicy()) {
    result_.SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_POLICY);
    return;
  }
  if (!WaitForSandboxReady()) {
    result_.SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_WAIT);
    return;
  }
  if (!InitApplyLimits()) {
    result_.SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_LIMITS);
    return;
  }
  // This call should be the last in the init sequence, because it can cause the
  // sandboxee to enter ptrace-stopped state, in which it will not be able to
  // send any messages over the Comms channel.
  if (!InitPtraceAttach()) {
    result_.SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_PTRACE);
    return;
  }

  // Tell the parent thread (Sandbox2 object) that we're done with the initial
  // set-up process of the sandboxee.
  decrement_count.reset();

  MainLoop(&sigtimedwait_sset);

  // Disarm the timer: it will be deleted in ~Monitor, but the Monitor object
  // lifetime is controlled by owner of Sandbox2, and we don't want to leave any
  // timers behind (esp. armed ones) in the meantime.
  TimerArm(absl::ZeroDuration());
}

bool Monitor::IsActivelyMonitoring() {
  // If we're still waiting for execve(), then we allow all syscalls.
  return !wait_for_execve_;
}

void Monitor::SetActivelyMonitoring() { wait_for_execve_ = false; }

void Monitor::MainSignals(int signo, siginfo_t* si) {
  VLOG(3) << "Signal '" << strsignal(signo) << "' (" << signo
          << ") received from PID: " << si->si_pid;

  // SIGCHLD is received frequently due to ptrace() events being sent by child
  // processes; return early to avoid costly syscalls.
  if (signo == SIGCHLD) {
    return;
  }

  // We should only receive signals from the same process (thread group). Other
  // signals are suspicious (esp. if coming from a sandboxed process) Using
  // syscall(__NR_getpid) here because getpid() is cached in glibc, and it
  // might return previous pid if bare syscall(__NR_fork) was used instead of
  // fork().
  //
  // The notable exception are signals caused by timer_settime which are sent
  // by the kernel.
  if (signo != Monitor::kTimerWallTimeSignal &&
      si->si_pid != util::Syscall(__NR_getpid)) {
    LOG(ERROR) << "Monitor received signal '" << strsignal(signo) << "' ("
               << signo << ") from PID " << si->si_pid
               << " which is not in the current thread group";
    return;
  }

  switch (signo) {
    case Monitor::kExternalKillSignal:
      VLOG(1) << "Will kill the main pid";
      ActionProcessKill(pid_, Result::EXTERNAL_KILL, 0);
      break;
    case Monitor::kTimerWallTimeSignal:
      VLOG(1) << "Sandbox process hit timeout due to the walltime timer";
      ActionProcessKill(pid_, Result::TIMEOUT, 0);
      break;
    case Monitor::kTimerSetSignal:
      VLOG(1) << "Will set the walltime timer to " << si->si_value.sival_int
              << " seconds";
      TimerArm(absl::Seconds(si->si_value.sival_int));
      break;
    case Monitor::kDumpStackSignal:
      VLOG(1) << "Dump the main pid's stack";
      should_dump_stack_ = true;
      PidInterrupt(pid_);
      break;
    default:
      LOG(ERROR) << "Unknown signal received: " << signo;
      break;
  }
}

// Not defined in glibc.
#define __WPTRACEEVENT(x) ((x & 0xff0000) >> 16)
bool Monitor::MainWait() {
  // All possible process status change event must be checked as SIGCHLD
  // is reported once only for all events that arrived at the same time.
  for (;;) {
    int status;
    // It should be a non-blocking operation (hence WNOHANG), so this function
    // returns quickly if there are no events to be processed.
    int ret = waitpid(-1, &status, __WNOTHREAD | __WALL | WUNTRACED | WNOHANG);

    // No traced processes have changed their status yet.
    if (ret == 0) {
      return false;
    }

    if (ret == -1 && errno == ECHILD) {
      LOG(ERROR) << "PANIC(). The main process has not exited yet, "
                 << "yet we haven't seen its exit event";
      // We'll simply exit which will kill all remaining processes (if
      // there are any) because of the PTRACE_O_EXITKILL ptrace() flag.
      return true;
    }
    if (ret == -1 && errno == EINTR) {
      VLOG(3) << "waitpid() interruped with EINTR";
      continue;
    }
    if (ret == -1) {
      PLOG(ERROR) << "waitpid() failed";
      continue;
    }

    VLOG(3) << "waitpid() returned with PID: " << ret << ", status: " << status;

    if (WIFEXITED(status)) {
      VLOG(1) << "PID: " << ret
              << " finished with code: " << WEXITSTATUS(status);
      // That's the main process, set the exit code, and exit. It will kill
      // all remaining processes (if there are any) because of the
      // PTRACE_O_EXITKILL ptrace() flag.
      if (ret == pid_) {
        if (IsActivelyMonitoring()) {
          result_.SetExitStatusCode(Result::OK, WEXITSTATUS(status));
        } else {
          result_.SetExitStatusCode(Result::SETUP_ERROR,
                                    Result::FAILED_MONITOR);
        }
        return true;
      }
    } else if (WIFSIGNALED(status)) {
      VLOG(1) << "PID: " << ret << " terminated with signal: "
              << util::GetSignalName(WTERMSIG(status));
      if (ret == pid_) {
        // That's the main process, depending on the result of the process take
        // the register content and/or the stack trace. The death of this
        // process will cause all remaining processes to be killed (if there are
        // any), see the PTRACE_O_EXITKILL ptrace() flag.

        // When the process is killed from a signal from within the result
        // status will be still unset, fix this.
        // The other cases should either be already handled, or (in the case of
        // Result::OK) should be impossible to reach.
        if (result_.final_status() == Result::UNSET) {
          result_.SetExitStatusCode(Result::SIGNALED, WTERMSIG(status));
        } else if (result_.final_status() == Result::OK) {
          LOG(ERROR) << "Unexpected codepath taken";
        }
        return true;
      }
    } else if (WIFSTOPPED(status)) {
      VLOG(2) << "PID: " << ret
              << " received signal: " << util::GetSignalName(WSTOPSIG(status))
              << " with event: " << __WPTRACEEVENT(status);
      StateProcessStopped(ret, status);
    } else if (WIFCONTINUED(status)) {
      VLOG(2) << "PID: " << ret << " is being continued";
    }
  }
}

void Monitor::MainLoop(sigset_t* sset) {
  for (;;) {
    // Use a time-out, so we can check for missed waitpid() events. It should
    // not happen during regular operations, so it's a defense-in-depth
    // mechanism against SIGCHLD signals being lost by the kernel (since these
    // are not-RT signals - i.e. not queued).
    static const timespec ts = {kWakeUpPeriodSec, kWakeUpPeriodNSec};

    // Wait for any kind of events, e.g. signals sent from the parent process,
    // or SIGCHLD sent by kernel indicating that state of one of the traced
    // processes has changed.
    siginfo_t si;
    int ret = sigtimedwait(sset, &si, &ts);
    if (ret > 0) {
      // Process signals which arrived.
      MainSignals(ret, &si);
    }

    // If CheckWait reported no more traced processes, or that
    // the main pid had exited, we should break this loop (i.e. our job is
    // done here).
    //
    // MainWait() should use a not-blocking (e.g. WNOHANG with waitpid())
    // syntax, so it returns quickly if there are not status changes in
    // traced processes.
    if (MainWait()) {
      return;
    }
  }
}

bool Monitor::InitSetupTimer() {
  walltime_timer_ = absl::make_unique<timer_t>();

  // Set the wall-time timer.
  sigevent sevp;
  sevp.sigev_value.sival_ptr = walltime_timer_.get();
  sevp.sigev_signo = kTimerWallTimeSignal;
  sevp.sigev_notify = SIGEV_THREAD_ID | SIGEV_SIGNAL;
  sevp._sigev_un._tid = static_cast<pid_t>(util::Syscall(__NR_gettid));
  // GLibc's implementation seem to mis-behave during timer_delete, as it's
  // trying to find out whether POSIX TIMERs are available. So, we stick to
  // syscalls for this class of calls.
  if (util::Syscall(__NR_timer_create, CLOCK_REALTIME,
                    reinterpret_cast<uintptr_t>(&sevp),
                    reinterpret_cast<uintptr_t>(walltime_timer_.get())) == -1) {
    walltime_timer_ = nullptr;
    PLOG(ERROR) << "timer_create(CLOCK_REALTIME, walltime_timer_)";
    return false;
  }
  return TimerArm(executor_->limits()->wall_time_limit());
}

// Can be used from a signal handler. Avoid non-reentrant functions.
bool Monitor::TimerArm(absl::Duration duration) {
  VLOG(2) << (duration == absl::ZeroDuration() ? "Disarming" : "Arming")
          << " the walltime timer with " << absl::FormatDuration(duration);

  itimerspec ts;
  absl::Duration rem;
  ts.it_value.tv_sec = absl::IDivDuration(duration, absl::Seconds(1), &rem);
  ts.it_value.tv_nsec = absl::ToInt64Nanoseconds(rem);
  ts.it_interval.tv_sec =
      duration != absl::ZeroDuration() ? 1L : 0L;  // Re-fire every 1 sec.
  ts.it_interval.tv_nsec = 0UL;
  itimerspec* null_ts = nullptr;
  if (util::Syscall(__NR_timer_settime,
                    reinterpret_cast<uintptr_t>(*walltime_timer_), 0,
                    reinterpret_cast<uintptr_t>(&ts),
                    reinterpret_cast<uintptr_t>(null_ts)) == -1) {
    PLOG(ERROR) << "timer_settime(): time: " << absl::FormatDuration(duration);
    return false;
  }

  return true;
}

void Monitor::CleanUpTimer() {
  if (walltime_timer_) {
    if (util::Syscall(__NR_timer_delete,
                      reinterpret_cast<uintptr_t>(*walltime_timer_)) == -1) {
      PLOG(ERROR) << "timer_delete()";
    }
  }
}

bool Monitor::InitSetupSig(int signo, sigset_t* sset) {
  // sigtimedwait will react (wake-up) to arrival of this signal.
  sigaddset(sset, signo);

  // Block this specific signal, so only sigtimedwait reacts to it.
  sigset_t block_set;
  if (sigemptyset(&block_set) == -1) {
    PLOG(ERROR) << "sigemptyset()";
    return false;
  }
  if (sigaddset(&block_set, signo) == -1) {
    PLOG(ERROR) << "sigaddset(" << signo << ")";
    return false;
  }
  if (pthread_sigmask(SIG_BLOCK, &block_set, nullptr) == -1) {
    PLOG(ERROR) << "pthread_sigmask(SIG_BLOCK, " << signo << ")";
    return false;
  }

  return true;
}

bool Monitor::InitSetupSignals(sigset_t* sset) {
  sigemptyset(sset);

  return Monitor::InitSetupSig(kExternalKillSignal, sset) &&
         Monitor::InitSetupSig(kTimerWallTimeSignal, sset) &&
         Monitor::InitSetupSig(kTimerSetSignal, sset) &&
         Monitor::InitSetupSig(kDumpStackSignal, sset) &&
         // SIGCHLD means that a new children process status change event
         // has been delivered (e.g. due ptrace notification).
         Monitor::InitSetupSig(SIGCHLD, sset);
}

bool Monitor::InitSendPolicy() {
  if (!policy_->SendPolicy(comms_)) {
    LOG(ERROR) << "Couldn't send policy";
    return false;
  }

  return true;
}

bool Monitor::InitSendCwd() {
  if (!comms_->SendString(executor_->cwd_)) {
    PLOG(ERROR) << "Couldn't send cwd";
    return false;
  }

  return true;
}

bool Monitor::InitApplyLimit(pid_t pid, __rlimit_resource resource,
                             const rlimit64& rlim) const {
  std::string rlim_name = absl::StrCat("UNKNOWN: ", resource);
  switch (resource) {
    case RLIMIT_AS:
      rlim_name = "RLIMIT_AS";
      break;
    case RLIMIT_FSIZE:
      rlim_name = "RLIMIT_FSIZE";
      break;
    case RLIMIT_NOFILE:
      rlim_name = "RLIMIT_NOFILE";
      break;
    case RLIMIT_CPU:
      rlim_name = "RLIMIT_CPU";
      break;
    case RLIMIT_CORE:
      rlim_name = "RLIMIT_CORE";
      break;
    default:
      break;
  }

  rlimit64 curr_limit;
  if (prlimit64(pid, resource, nullptr, &curr_limit) == -1) {
    PLOG(ERROR) << "prlimit64(" << pid << ", " << rlim_name << ")";
  } else {
    // In such case, don't update the limits, as it will fail. Just stick to the
    // current ones (which are already lower than intended).
    if (rlim.rlim_cur > curr_limit.rlim_max) {
      LOG(ERROR) << rlim_name << ": new.current > current.max ("
                 << rlim.rlim_cur << " > " << curr_limit.rlim_max
                 << "), skipping";
      return true;
    }
  }
  if (prlimit64(pid, resource, &rlim, nullptr) == -1) {
    PLOG(ERROR) << "prlimit64(RLIMIT_AS, " << rlim.rlim_cur << ")";
    return false;
  }

  return true;
}

bool Monitor::InitApplyLimits() {
  Limits* limits = executor_->limits();
  return InitApplyLimit(pid_, RLIMIT_AS, limits->rlimit_as()) &&
         InitApplyLimit(pid_, RLIMIT_CPU, limits->rlimit_cpu()) &&
         InitApplyLimit(pid_, RLIMIT_FSIZE, limits->rlimit_fsize()) &&
         InitApplyLimit(pid_, RLIMIT_NOFILE, limits->rlimit_nofile()) &&
         InitApplyLimit(pid_, RLIMIT_CORE, limits->rlimit_core());
}

bool Monitor::InitSendIPC() { return ipc_->SendFdsOverComms(); }

bool Monitor::WaitForSandboxReady() {
  uint32_t tmp;
  if (!comms_->RecvUint32(&tmp)) {
    LOG(ERROR) << "Couldn't receive 'Client::kClient2SandboxReady' message";
    return false;
  }
  if (tmp != Client::kClient2SandboxReady) {
    LOG(ERROR) << "Received " << tmp << " != Client::kClient2SandboxReady ("
               << Client::kClient2SandboxReady << ")";
    return false;
  }
  return true;
}

bool Monitor::InitPtraceAttach() {
  sanitizer::WaitForTsan();

  // Get a list of tasks.
  std::set<int> tasks;
  if (!sanitizer::GetListOfTasks(pid_, &tasks)) {
    LOG(ERROR) << "Could not get list of tasks";
    return false;
  }

  // With TSYNC, we can allow threads: seccomp applies to all threads.

  if (tasks.size() > 1) {
    LOG(WARNING) << "PID " << pid_ << " has " << tasks.size() << " threads,"
                 << " at the time of call to SandboxMeHere. If you are seeing"
                 << " more sandbox violations than expected, this might be"
                 << " the reason why"
                 << ".";
  }

  intptr_t ptrace_opts =
      PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK |
      PTRACE_O_TRACEVFORKDONE | PTRACE_O_TRACECLONE | PTRACE_O_TRACEEXEC |
      PTRACE_O_TRACEEXIT | PTRACE_O_TRACESECCOMP | PTRACE_O_EXITKILL;

  bool main_pid_found = false;
  for (auto task : tasks) {
    if (task == pid_) {
      main_pid_found = true;
    }

    // In some situations we allow ptrace to try again when it fails.
    bool ptrace_succeeded = false;
    int retries = 0;
    auto deadline = absl::Now() + absl::Seconds(2);
    while (absl::Now() < deadline) {
      int ret = ptrace(PTRACE_SEIZE, task, 0, ptrace_opts);
      if (ret == 0) {
        ptrace_succeeded = true;
        break;
      }
      if (ret != 0 && errno == ESRCH) {
        // A task may have exited since we captured the task list, we will allow
        // things to continue after we log a warning.
        PLOG(WARNING) << "ptrace(PTRACE_SEIZE, " << task << ", "
                      << absl::StrCat("0x", absl::Hex(ptrace_opts))
                      << ") skipping exited task. Continuing with other tasks.";
        ptrace_succeeded = true;
        break;
      }
      if (ret != 0 && errno == EPERM) {
        // Sometimes when a task is exiting we can get an EPERM from ptrace.
        // Let's try again up until the timeout in this situation.
        PLOG(WARNING) << "ptrace(PTRACE_SEIZE, " << task << ", "
                      << absl::StrCat("0x", absl::Hex(ptrace_opts))
                      << "), trying again...";

        // Exponential Backoff.
        constexpr auto kInitialRetry = absl::Milliseconds(1);
        constexpr auto kMaxRetry = absl::Milliseconds(20);
        const auto retry_interval =
            kInitialRetry * (1 << std::min(10, retries++));
        absl::SleepFor(std::min(retry_interval, kMaxRetry));
        continue;
      }

      // Any other errno will be considered a failure.
      PLOG(ERROR) << "ptrace(PTRACE_SEIZE, " << task << ", "
                  << absl::StrCat("0x", absl::Hex(ptrace_opts)) << ") failed.";
      return false;
    }

    if (!ptrace_succeeded) {
      LOG(ERROR) << "ptrace(PTRACE_SEIZE, " << task << ", "
                 << absl::StrCat("0x", absl::Hex(ptrace_opts))
                 << ") failed after retrying until the timeout.";
      return false;
    }
  }

  if (!main_pid_found) {
    LOG(ERROR) << "The pid " << pid_ << " was not found in its own tasklist.";
    return false;
  }

  // Get a list of tasks after attaching.
  std::set<int> tasks_after;
  if (!sanitizer::GetListOfTasks(pid_, &tasks_after)) {
    LOG(ERROR) << "Could not get list of tasks";
    return false;
  }

  // Check that no new threads have shown up. Note: tasks_after can have fewer
  // tasks than before but no new tasks can be added as they would be missing
  // from the initial task list.
  if (!std::includes(tasks.begin(), tasks.end(), tasks_after.begin(),
                     tasks_after.end())) {
    LOG(ERROR) << "The pid " << pid_
               << " spawned new threads while we were trying to attach to it.";
    return false;
  }

  // No glibc wrapper for gettid - see 'man gettid'.
  VLOG(1) << "Monitor (PID: " << getpid()
          << ", TID: " << util::Syscall(__NR_gettid)
          << ") attached to PID: " << pid_;

  // Technically, the sandboxee can be in a ptrace-stopped state right now,
  // because some signal could have arrived in the meantime. Yet, this
  // Comms::SendUint32 call shouldn't lock our process, because the underlying
  // socketpair() channel is buffered, hence it will accept the uint32_t message
  // no matter what is the current state of the sandboxee, and it will allow for
  // our process to continue and unlock the sandboxee with the proper ptrace
  // event handling.
  if (!comms_->SendUint32(Client::kSandbox2ClientDone)) {
    LOG(ERROR) << "Couldn't send Client::kSandbox2ClientDone message";
    return false;
  }
  return true;
}

bool Monitor::InitAcceptConnection() {
  // It's a pre-connected Comms channel, no need to accept new connection or
  // verify the peer (sandboxee).
  if (comms_->IsConnected()) {
    return true;
  }

  if (!comms_->Accept()) {
    return false;
  }

  // Check whether the PID which has connected to us, is the PID we're
  // expecting.
  pid_t cred_pid;
  uid_t cred_uid;
  gid_t cred_gid;
  if (!comms_->RecvCreds(&cred_pid, &cred_uid, &cred_gid)) {
    LOG(ERROR) << "Couldn't receive credentials";
    return false;
  }

  if (pid_ != cred_pid) {
    LOG(ERROR) << "Initial PID (" << pid_ << ") differs from the PID received "
               << "from the peer (" << cred_pid << ")";
    return false;
  }

  return true;
}

void Monitor::ActionProcessContinue(pid_t pid, int signo) {
  if (ptrace(PTRACE_CONT, pid, 0, signo) == -1) {
    PLOG(ERROR) << "ptrace(PTRACE_CONT, pid=" << pid << ", sig=" << signo
                << ")";
  }
}

void Monitor::ActionProcessStop(pid_t pid, int signo) {
  if (ptrace(PTRACE_LISTEN, pid, 0, signo) == -1) {
    PLOG(ERROR) << "ptrace(PTRACE_LISTEN, pid=" << pid << ", sig=" << signo
                << ")";
  }
}

void Monitor::ActionProcessSyscall(Regs* regs, const Syscall& syscall) {
  // If the sandboxing is not enabled yet, allow the first __NR_execveat.
  if (syscall.nr() == __NR_execveat && !IsActivelyMonitoring()) {
    VLOG(1) << "[PERMITTED/BEFORE_EXECVEAT]: "
            << "SYSCALL ::: PID: " << regs->pid() << ", PROG: '"
            << util::GetProgName(regs->pid())
            << "' : " << syscall.GetDescription();
    ActionProcessContinue(regs->pid(), 0);
    return;
  }

  // Notify can decide whether we want to allow this syscall. It could be useful
  // for sandbox setups in which some syscalls might still need some logging,
  // but nonetheless be allowed ('permissible syscalls' in sandbox v1).
  if (notify_->EventSyscallTrap(syscall)) {
    LOG(WARNING) << "[PERMITTED]: SYSCALL ::: PID: " << regs->pid()
                 << ", PROG: '" << util::GetProgName(regs->pid())
                 << "' : " << syscall.GetDescription();

    ActionProcessContinue(regs->pid(), 0);
    return;
  }

  // TODO(wiktorg): Further clean that up, probably while doing monitor cleanup
  // log_file_ not null iff FLAGS_sandbox2_danger_danger_permit_all_and_log is
  // set.
  if (log_file_) {
    std::string syscall_description = syscall.GetDescription();
    PCHECK(absl::FPrintF(log_file_, "PID: %d %s\n", regs->pid(),
                         syscall_description) >= 0);
    ActionProcessContinue(regs->pid(), 0);
    return;
  }

  if (absl::GetFlag(FLAGS_sandbox2_danger_danger_permit_all)) {
    ActionProcessContinue(regs->pid(), 0);
    return;
  }

  ActionProcessSyscallViolation(regs, syscall, kSyscallViolation);
}

void Monitor::ActionProcessSyscallViolation(Regs* regs, const Syscall& syscall,
                                            ViolationType violation_type) {
  pid_t pid = regs->pid();

  LogAccessViolation(syscall);
  notify_->EventSyscallViolation(syscall, violation_type);
  result_.SetExitStatusCode(Result::VIOLATION, syscall.nr());
  result_.SetSyscall(absl::make_unique<Syscall>(syscall));
  // Only get the stacktrace if we are not in the libunwind sandbox (avoid
  // recursion).
  if (executor_->libunwind_sbox_for_pid_ == 0 && policy_->GetNamespace()) {
    if (policy_->collect_stacktrace_on_violation_) {
      result_.SetStackTrace(
          GetStackTrace(regs, policy_->GetNamespace()->mounts()));
      LOG(ERROR) << "Stack trace: " << result_.GetStackTrace();
    } else {
      LOG(ERROR) << "Stack traces have been disabled";
    }
  }
  // We make the result object create its own Reg instance. our regs is a
  // pointer to a stack variable which might not live long enough.
  result_.LoadRegs(pid);
  result_.SetProgName(util::GetProgName(pid));
  result_.SetProcMaps(ReadProcMaps(pid_));

  // Rewrite the syscall argument to something invalid (-1). The process will
  // be killed by ActionProcessKill(), so this is just a precaution.
  auto status = regs->SkipSyscallReturnValue(-ENOSYS);
  if (!status.ok()) {
    LOG(ERROR) << status;
  }

  ActionProcessKill(pid, Result::VIOLATION, syscall.nr());
}

void Monitor::LogAccessViolation(const Syscall& syscall) {
  // Do not unwind libunwind.
  if (executor_->libunwind_sbox_for_pid_ != 0) {
    LOG(ERROR) << "Sandbox violation during execution of libunwind: "
               << syscall.GetDescription();
    return;
  }

  uintptr_t syscall_nr = syscall.nr();
  uintptr_t arg0 = syscall.args()[0];

  // So, this is an invalid syscall. Will be killed by seccomp-bpf policies as
  // well, but we should be on a safe side here as well.
  LOG(ERROR) << "SANDBOX VIOLATION : PID: " << syscall.pid() << ", PROG: '"
             << util::GetProgName(syscall.pid())
             << "' : " << syscall.GetDescription();

  // This follows policy in Policy::GetDefaultPolicy - keep it in sync.
  if (syscall.arch() != Syscall::GetHostArch()) {
    LOG(ERROR)
        << "This is a violation because the syscall was issued because the"
        << " sandboxee and executor architectures are different.";
    return;
  }

  if (syscall_nr == __NR_ptrace) {
    LOG(ERROR)
        << "This is a violation because the ptrace syscall would be unsafe in"
        << " sandbox2, so it has been blocked.";
    return;
  }
  if (syscall_nr == __NR_bpf) {
    LOG(ERROR)
        << "This is a violation because the bpf syscall would be risky in"
        << " a sandbox, so it has been blocked.";
    return;
  }

  if (syscall_nr == __NR_clone && ((arg0 & CLONE_UNTRACED) != 0)) {
    LOG(ERROR) << "This is a violation because calling clone with CLONE_UNTRACE"
               << " would be unsafe in sandbox2, so it has been blocked.";
    return;
  }
}

void Monitor::ActionProcessKill(pid_t pid, Result::StatusEnum status,
                                uintptr_t code) {
  // Avoid overwriting result if we set it for instance after a violation.
  if (result_.final_status() == Result::UNSET) {
    result_.SetExitStatusCode(status, code);
  }

  VLOG(1) << "Sending SIGKILL to the PID: " << pid_;
  if (kill(pid_, SIGKILL) != 0) {
    LOG(FATAL) << "Could not send SIGKILL to PID " << pid_;
  }
}

void Monitor::EventPtraceSeccomp(pid_t pid, int event_msg) {
  VLOG(1) << "PID: " << pid << " violation uncovered via the SECCOMP_EVENT";
  // If the seccomp-policy is using RET_TRACE, we request that it returns the
  // syscall architecture identifier in the SECCOMP_RET_DATA.
  const auto syscall_arch = static_cast<Syscall::CpuArch>(event_msg);
  Regs regs(pid);
  auto status = regs.Fetch();
  if (!status.ok()) {
    LOG(ERROR) << status;
    ActionProcessKill(pid, Result::INTERNAL_ERROR, Result::FAILED_FETCH);
    return;
  }

  Syscall syscall = regs.ToSyscall(syscall_arch);
  // If the architecture of the syscall used is different that the current host
  // architecture, report a violation.
  if (syscall_arch != Syscall::GetHostArch()) {
    ActionProcessSyscallViolation(&regs, syscall, kArchitectureSwitchViolation);
    return;
  }

  ActionProcessSyscall(&regs, syscall);
}

void Monitor::EventPtraceExec(pid_t pid, int event_msg) {
  if (!IsActivelyMonitoring()) {
    VLOG(1) << "PTRACE_EVENT_EXEC seen from PID: " << event_msg
            << ". SANDBOX ENABLED!";
    SetActivelyMonitoring();
  }
  ActionProcessContinue(pid, 0);
}

void Monitor::EventPtraceExit(pid_t pid, int event_msg) {
  // A regular exit, let it continue.
  if (WIFEXITED(event_msg)) {
    ActionProcessContinue(pid, 0);
    return;
  }

  // Everything except the SECCOMP violation can continue.
  if (!WIFSIGNALED(event_msg) || WTERMSIG(event_msg) != SIGSYS) {
    // Process is dying because it received a signal.
    // This can occur in three cases:
    // 1) Process was killed from the sandbox, in this case the result status
    //    was already set to Result::EXTERNAL_KILL. We do not get the stack
    //    trace in this case.
    // 2) Process was killed because it hit a timeout. The result status is
    //    also already set, however we are interested in the stack trace.
    // 3) Regular signal. We need to obtain everything. The status will be set
    //    upon the process exit handler.
    if (pid == pid_) {
      result_.LoadRegs(pid_);
      result_.SetProgName(util::GetProgName(pid_));
      result_.SetProcMaps(ReadProcMaps(pid_));
      bool stacktrace_collection_possible =
          policy_->GetNamespace() && executor_->libunwind_sbox_for_pid_ == 0;
      auto collect_stacktrace = [this]() {
        result_.SetStackTrace(GetStackTrace(result_.GetRegs(),
                                            policy_->GetNamespace()->mounts()));
      };
      switch (result_.final_status()) {
        case Result::EXTERNAL_KILL:
          if (stacktrace_collection_possible &&
              policy_->collect_stacktrace_on_kill_) {
            collect_stacktrace();
          }
          break;
        case Result::TIMEOUT:
          if (stacktrace_collection_possible &&
              policy_->collect_stacktrace_on_timeout_) {
            collect_stacktrace();
          }
          break;
        case Result::VIOLATION:
          break;
        case Result::UNSET:
          // Regular signal.
          if (stacktrace_collection_possible &&
              policy_->collect_stacktrace_on_signal_) {
            collect_stacktrace();
          }
          break;
        default:
          LOG(ERROR) << "Unexpected codepath taken";
          break;
      }
    }

    ActionProcessContinue(pid, 0);
    return;
  }

  VLOG(1) << "PID: " << pid << " violation uncovered via the EXIT_EVENT";

  // We do not generate the stack trace in the SECCOMP case as it will be
  // generated during ActionProcessSyscallViolation anyway.
  Regs regs(pid);
  auto status = regs.Fetch();
  if (!status.ok()) {
    LOG(ERROR) << status;
    ActionProcessKill(pid, Result::INTERNAL_ERROR, Result::FAILED_FETCH);
    return;
  }

  auto syscall = regs.ToSyscall(Syscall::GetHostArch());

  ActionProcessSyscallViolation(&regs, syscall, kSyscallViolation);
}

void Monitor::EventPtraceStop(pid_t pid, int stopsig) {
  // It's not a real stop signal. For example PTRACE_O_TRACECLONE and similar
  // flags to ptrace(PTRACE_SEIZE) might generate this event with SIGTRAP.
  if (stopsig != SIGSTOP && stopsig != SIGTSTP && stopsig != SIGTTIN &&
      stopsig != SIGTTOU) {
    ActionProcessContinue(pid, 0);
    return;
  }
  // It's our PID stop signal. Stop it.
  VLOG(2) << "PID: " << pid << " stopped due to "
          << util::GetSignalName(stopsig);
  ActionProcessStop(pid, 0);
}

void Monitor::StateProcessStopped(pid_t pid, int status) {
  int stopsig = WSTOPSIG(status);
  if (__WPTRACEEVENT(status) == 0) {
    // Must be a regular signal delivery.
    VLOG(2) << "PID: " << pid
            << " received signal: " << util::GetSignalName(stopsig);
    notify_->EventSignal(pid, stopsig);
    ActionProcessContinue(pid, stopsig);
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
    ActionProcessKill(pid, Result::INTERNAL_ERROR, Result::FAILED_GETEVENT);
    return;
  }

  if (pid == pid_ && should_dump_stack_ &&
      executor_->libunwind_sbox_for_pid_ == 0 && policy_->GetNamespace()) {
    Regs regs(pid);
    auto status = regs.Fetch();
    if (status.ok()) {
      VLOG(0) << "SANDBOX STACK : PID: " << pid << ", ["
              << GetStackTrace(&regs, policy_->GetNamespace()->mounts()) << "]";
    } else {
      LOG(WARNING) << "FAILED TO GET SANDBOX STACK : " << status;
    }
    should_dump_stack_ = false;
  }

#if !defined(PTRACE_EVENT_STOP)
#define PTRACE_EVENT_STOP 128
#endif

  switch (__WPTRACEEVENT(status)) {
    case PTRACE_EVENT_FORK:
      /* fall through */
    case PTRACE_EVENT_VFORK:
      /* fall through */
    case PTRACE_EVENT_CLONE:
      /* fall through */
    case PTRACE_EVENT_VFORK_DONE:
      ActionProcessContinue(pid, 0);
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

void Monitor::PidInterrupt(pid_t pid) {
  if (ptrace(PTRACE_INTERRUPT, pid, 0, 0) == -1) {
    PLOG(WARNING) << "ptrace(PTRACE_INTERRUPT, pid=" << pid << ")";
  }
}

}  // namespace sandbox2
