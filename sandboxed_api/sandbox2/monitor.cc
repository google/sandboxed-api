// Copyright 2019 Google LLC
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

// clang-format off
#include <linux/posix_types.h>  // NOLINT: Needs to come before linux/ipc.h
#include <linux/ipc.h>
// clang-format on
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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>

#include <glog/logging.h>
#include "absl/cleanup/cleanup.h"
#include "sandboxed_api/util/flag.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/limits.h"
#include "sandboxed_api/sandbox2/mounts.h"
#include "sandboxed_api/sandbox2/namespace.h"
#include "sandboxed_api/sandbox2/network_proxy/server.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/regs.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sanitizer.h"
#include "sandboxed_api/sandbox2/stack_trace.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/util/file_helpers.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/status_macros.h"
#include "sandboxed_api/util/temp_file.h"

using std::string;

ABSL_FLAG(bool, sandbox2_report_on_sandboxee_signal, true,
          "Report sandbox2 sandboxee deaths caused by signals");

ABSL_FLAG(bool, sandbox2_report_on_sandboxee_timeout, true,
          "Report sandbox2 sandboxee timeouts");

ABSL_DECLARE_FLAG(bool, sandbox2_danger_danger_permit_all);
ABSL_DECLARE_FLAG(bool, sandbox_libunwind_crash_handler);
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

void InterruptProcess(pid_t pid) {
  if (ptrace(PTRACE_INTERRUPT, pid, 0, 0) == -1) {
    PLOG(WARNING) << "ptrace(PTRACE_INTERRUPT, pid=" << pid << ")";
  }
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
      PLOG(ERROR) << "ptrace(PTRACE_CONT, pid=" << pid << ", sig=" << signo
                  << ")";
    }
  }
}

void MaybeEnableTomoyoLsmWorkaround(Mounts& mounts, std::string& comms_fd_dev) {
  static auto tomoyo_active = []() -> bool {
    std::string lsm_list;
    if (auto status = sapi::file::GetContents(
            "/sys/kernel/security/lsm", &lsm_list, sapi::file::Defaults());
        !status.ok() && !absl::IsNotFound(status)) {
      SAPI_RAW_PLOG(WARNING, "Checking active LSMs failed: %s",
                    std::string(status.message()).c_str());
      return false;
    }
    return absl::StrContains(lsm_list, "tomoyo");
  }();

  if (!tomoyo_active) {
    return;
  }
  SAPI_RAW_VLOG(1, "Tomoyo LSM active, enabling workaround");

  if (mounts.ResolvePath("/dev").ok() || mounts.ResolvePath("/dev/fd").ok()) {
    // Avoid shadowing /dev/fd/1022 below if /dev or /dev/fd is already mapped.
    SAPI_RAW_VLOG(1, "Parent dir already mapped, skipping");
    return;
  }

  auto temp_file = sapi::CreateNamedTempFileAndClose("/tmp/");
  if (!temp_file.ok()) {
    SAPI_RAW_LOG(WARNING, "Failed to create empty temp file: %s",
                 std::string(temp_file.status().message()).c_str());
    return;
  }
  comms_fd_dev = std::move(*temp_file);

  // Ignore errors here, as the file itself might already be mapped.
  if (auto status = mounts.AddFileAt(
          comms_fd_dev, absl::StrCat("/dev/fd/", Comms::kSandbox2TargetExecFD),
          false);
      !status.ok()) {
    SAPI_RAW_VLOG(1, "Mapping comms FD: %s",
                  std::string(status.message()).c_str());
  }
}

}  // namespace

Monitor::Monitor(Executor* executor, Policy* policy, Notify* notify)
    : executor_(executor),
      notify_(notify),
      policy_(policy),
      // NOLINTNEXTLINE clang-diagnostic-deprecated-declarations
      comms_(executor_->ipc()->comms()),
      ipc_(executor_->ipc()),
      wait_for_execve_(executor->enable_sandboxing_pre_execve_) {
  // It's a pre-connected Comms channel, no need to accept new connection.
  CHECK(comms_->IsConnected());
  std::string path =
      absl::GetFlag(FLAGS_sandbox2_danger_danger_permit_all_and_log);
  external_kill_request_flag_.test_and_set(std::memory_order_relaxed);
  dump_stack_request_flag_.test_and_set(std::memory_order_relaxed);
  if (!path.empty()) {
    log_file_ = std::fopen(path.c_str(), "a+");
    PCHECK(log_file_ != nullptr) << "Failed to open log file '" << path << "'";
  }

  if (auto* ns = policy_->GetNamespace(); ns) {
    // Check for the Tomoyo LSM, which is active by default in several common
    // distribution kernels (esp. Debian).
    MaybeEnableTomoyoLsmWorkaround(ns->mounts(), comms_fd_dev_);
  }
}

Monitor::~Monitor() {
  if (!comms_fd_dev_.empty()) {
    std::remove(comms_fd_dev_.c_str());
  }
  if (log_file_) {
    std::fclose(log_file_);
  }
  if (network_proxy_server_) {
    network_proxy_thread_.join();
  }
}

namespace {

void LogContainer(const std::vector<std::string>& container) {
  for (size_t i = 0; i < container.size(); ++i) {
    SAPI_RAW_LOG(INFO, "[%4zd]=%s", i, container[i].c_str());
  }
}

}  // namespace

void Monitor::Run() {
  absl::Cleanup setup_notify = [this] { setup_notification_.Notify(); };

  absl::Cleanup monitor_cleanup = [this] {
    getrusage(RUSAGE_THREAD, result_.GetRUsageMonitor());
    notify_->EventFinished(result_);
    ipc_->InternalCleanupFdMap();
    done_notification_.Notify();
  };

  if (executor_->limits()->wall_time_limit() != absl::ZeroDuration()) {
    auto deadline = absl::Now() + executor_->limits()->wall_time_limit();
    deadline_millis_.store(absl::ToUnixMillis(deadline),
                           std::memory_order_relaxed);
  }

  // It'd be costly to initialize the sigset_t for each sigtimedwait()
  // invocation, so do it once per Monitor.
  sigset_t sigtimedwait_sset;
  if (!InitSetupSignals(&sigtimedwait_sset)) {
    SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_SIGNALS);
    return;
  }

  Namespace* ns = policy_->GetNamespace();
  if (SAPI_VLOG_IS_ON(1) && ns != nullptr) {
    std::vector<std::string> outside_entries;
    std::vector<std::string> inside_entries;
    ns->mounts().RecursivelyListMounts(
        /*outside_entries=*/&outside_entries,
        /*inside_entries=*/&inside_entries);
    SAPI_RAW_VLOG(1, "Outside entries mapped to chroot:");
    LogContainer(outside_entries);
    SAPI_RAW_VLOG(1, "Inside entries as they appear in chroot:");
    LogContainer(inside_entries);
  }

  // Don't trace the child: it will allow to use 'strace -f' with the whole
  // sandbox master/monitor, which ptrace_attach'es to the child.
  int clone_flags = CLONE_UNTRACED;

  if (policy_->allowed_hosts_) {
    EnableNetworkProxyServer();
  }

  // Get PID of the sandboxee.
  pid_t init_pid = 0;
  bool should_have_init = ns && (ns->GetCloneFlags() & CLONE_NEWPID);
  pid_ = executor_->StartSubProcess(clone_flags, ns, policy_->GetCapabilities(),
                                    &init_pid);

  if (init_pid > 0) {
    if (ptrace(PTRACE_SEIZE, init_pid, 0, PTRACE_O_EXITKILL) != 0) {
      if (errno == ESRCH) {
        SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_PTRACE);
        return;
      }
      PLOG(FATAL) << "attaching to init process failed";
    }
  }

  if (pid_ <= 0 || (should_have_init && init_pid <= 0)) {
    SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_SUBPROCESS);
    return;
  }

  if (!notify_->EventStarted(pid_, comms_)) {
    SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_NOTIFY);
    return;
  }
  if (!InitSendIPC()) {
    SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_IPC);
    return;
  }
  if (!InitSendCwd()) {
    SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_CWD);
    return;
  }
  if (!InitSendPolicy()) {
    SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_POLICY);
    return;
  }
  if (!WaitForSandboxReady()) {
    SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_WAIT);
    return;
  }
  if (!InitApplyLimits()) {
    SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_LIMITS);
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

  MainLoop(&sigtimedwait_sset);
}

bool Monitor::IsActivelyMonitoring() {
  // If we're still waiting for execve(), then we allow all syscalls.
  return !wait_for_execve_;
}

void Monitor::SetActivelyMonitoring() { wait_for_execve_ = false; }

void Monitor::SetExitStatusCode(Result::StatusEnum final_status,
                                uintptr_t reason_code) {
  CHECK(result_.final_status() == Result::UNSET);
  result_.SetExitStatusCode(final_status, reason_code);
}

bool Monitor::ShouldCollectStackTrace() {
  // Only get the stacktrace if we are not in the libunwind sandbox (avoid
  // recursion).
  bool stacktrace_collection_possible =
      (policy_->GetNamespace() ||
       absl::GetFlag(FLAGS_sandbox_libunwind_crash_handler) == false) &&
      executor_->libunwind_sbox_for_pid_ == 0;
  if (!stacktrace_collection_possible) {
    LOG(ERROR) << "Cannot collect stack trace. Unwind pid "
               << executor_->libunwind_sbox_for_pid_ << ", namespace "
               << policy_->GetNamespace();
    return false;
  }
  switch (result_.final_status()) {
    case Result::EXTERNAL_KILL:
      return policy_->collect_stacktrace_on_kill_;
    case Result::TIMEOUT:
      return policy_->collect_stacktrace_on_timeout_;
    case Result::SIGNALED:
      return policy_->collect_stacktrace_on_signal_;
    case Result::VIOLATION:
      return policy_->collect_stacktrace_on_violation_;
    default:
      return false;
  }
}

void Monitor::SetAdditionalResultInfo(std::unique_ptr<Regs> regs) {
  pid_t pid = regs->pid();
  result_.SetRegs(std::move(regs));
  result_.SetProgName(util::GetProgName(pid));
  result_.SetProcMaps(ReadProcMaps(pid_));
  if (!ShouldCollectStackTrace()) {
    LOG(INFO) << "Stack traces have been disabled";
    return;
  }
  auto* ns = policy_->GetNamespace();
  const Mounts empty_mounts;
  absl::StatusOr<std::vector<std::string>> stack_trace =
      GetStackTrace(result_.GetRegs(), ns ? ns->mounts() : empty_mounts);

  if (!stack_trace.ok()) {
    LOG(ERROR) << "Could not obtain stack trace: " << stack_trace.status();
    return;
  }

  result_.set_stack_trace(*stack_trace);

  LOG(INFO) << "Stack trace: [";
  for (const auto& frame : CompactStackTrace(result_.stack_trace())) {
    LOG(INFO) << "  " << frame;
  }
  LOG(INFO) << "]";
}

void Monitor::KillSandboxee() {
  VLOG(1) << "Sending SIGKILL to the PID: " << pid_;
  if (kill(pid_, SIGKILL) != 0) {
    LOG(ERROR) << "Could not send SIGKILL to PID " << pid_;
    SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_KILL);
  }
}

// Not defined in glibc.
#define __WPTRACEEVENT(x) ((x & 0xff0000) >> 16)

void Monitor::MainLoop(sigset_t* sset) {
  bool sandboxee_exited = false;
  int status;
  // All possible still running children of main process, will be killed due to
  // PTRACE_O_EXITKILL ptrace() flag.
  while (result_.final_status() == Result::UNSET) {
    int64_t deadline = deadline_millis_.load(std::memory_order_relaxed);
    if (deadline != 0 && absl::Now() >= absl::FromUnixMillis(deadline)) {
      VLOG(1) << "Sandbox process hit timeout due to the walltime timer";
      timed_out_ = true;
      KillSandboxee();
    }

    if (!dump_stack_request_flag_.test_and_set(std::memory_order_relaxed)) {
      should_dump_stack_ = true;
      InterruptProcess(pid_);
    }

    if (!external_kill_request_flag_.test_and_set(std::memory_order_relaxed)) {
      external_kill_ = true;
      KillSandboxee();
    }

    if (network_proxy_server_ &&
        network_proxy_server_->violation_occurred_.load(
            std::memory_order_acquire) &&
        !network_violation_) {
      network_violation_ = true;
      KillSandboxee();
    }

    // It should be a non-blocking operation (hence WNOHANG), so this function
    // returns quickly if there are no events to be processed.
    // Prioritize main pid to avoid resource starvation
    pid_t ret =
        waitpid(pid_, &status, __WNOTHREAD | __WALL | WUNTRACED | WNOHANG);
    if (ret == 0) {
      ret = waitpid(-1, &status, __WNOTHREAD | __WALL | WUNTRACED | WNOHANG);
    }

    if (ret == 0) {
      constexpr timespec ts = {kWakeUpPeriodSec, kWakeUpPeriodNSec};
      int signo = sigtimedwait(sset, nullptr, &ts);
      LOG_IF(ERROR, signo != -1 && signo != SIGCHLD)
          << "Unknown signal received: " << signo;
      continue;
    }

    if (ret == -1) {
      if (errno == ECHILD) {
        LOG(ERROR) << "PANIC(). The main process has not exited yet, "
                   << "yet we haven't seen its exit event";
        SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_CHILD);
      } else {
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
      if (ret == pid_) {
        if (IsActivelyMonitoring()) {
          SetExitStatusCode(Result::OK, WEXITSTATUS(status));
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
      if (ret == pid_) {
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
  // Try to make sure main pid is killed and reaped
  if (!sandboxee_exited) {
    kill(pid_, SIGKILL);
    constexpr auto kGracefullExitTimeout = absl::Milliseconds(200);
    auto deadline = absl::Now() + kGracefullExitTimeout;
    for (;;) {
      auto left = deadline - absl::Now();
      if (absl::Now() >= deadline) {
        LOG(INFO) << "Waiting for sandboxee exit timed out";
        break;
      }
      pid_t ret =
          waitpid(pid_, &status, __WNOTHREAD | __WALL | WUNTRACED | WNOHANG);
      if (ret == 0) {
        // Sometimes PTRACE_EVENT_EXIT needs to be handled for each child thread
        // in order to observe main thread exit
        ret = waitpid(-1, &status, __WNOTHREAD | __WALL | WUNTRACED | WNOHANG);
      }
      if (ret == -1) {
        PLOG(ERROR) << "waitpid() failed";
        break;
      }
      if (ret == pid_ && (WIFSIGNALED(status) || WIFEXITED(status))) {
        break;
      }
      if (ret == 0) {
        auto ts = absl::ToTimespec(left);
        sigtimedwait(sset, nullptr, &ts);
      } else if (WIFSTOPPED(status) &&
                 __WPTRACEEVENT(status) == PTRACE_EVENT_EXIT) {
        VLOG(2) << "PID: " << ret << " PTRACE_EVENT_EXIT ";
        ContinueProcess(ret, 0);
      } else {
        kill(pid_, SIGKILL);
      }
    }
  }
}

bool Monitor::InitSetupSignals(sigset_t* sset) {
  if (sigemptyset(sset) == -1) {
    PLOG(ERROR) << "sigemptyset()";
    return false;
  }

  // sigtimedwait will react (wake-up) to arrival of this signal.
  if (sigaddset(sset, SIGCHLD) == -1) {
    PLOG(ERROR) << "sigaddset(SIGCHLD)";
    return false;
  }

  if (pthread_sigmask(SIG_BLOCK, sset, nullptr) == -1) {
    PLOG(ERROR) << "pthread_sigmask(SIG_BLOCK, SIGCHLD)";
    return false;
  }

  return true;
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
  rlimit64 curr_limit;
  if (prlimit64(pid, resource, nullptr, &curr_limit) == -1) {
    PLOG(ERROR) << "prlimit64(" << pid << ", " << util::GetRlimitName(resource)
                << ")";
  } else if (rlim.rlim_cur > curr_limit.rlim_max) {
    // In such case, don't update the limits, as it will fail. Just stick to the
    // current ones (which are already lower than intended).
    LOG(ERROR) << util::GetRlimitName(resource)
               << ": new.current > current.max (" << rlim.rlim_cur << " > "
               << curr_limit.rlim_max << "), skipping";
    return true;
  }

  if (prlimit64(pid, resource, &rlim, nullptr) == -1) {
    PLOG(ERROR) << "prlimit64(" << pid << ", " << util::GetRlimitName(resource)
                << ", " << rlim.rlim_cur << ")";
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

  if (tasks.find(pid_) == tasks.end()) {
    LOG(ERROR) << "The pid " << pid_ << " was not found in its own tasklist.";
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

  std::set<int> tasks_attached;
  int retries = 0;
  absl::Time deadline = absl::Now() + absl::Seconds(2);

  // In some situations we allow ptrace to try again when it fails.
  while (!tasks.empty()) {
    std::set<int> tasks_left;
    for (int task : tasks) {
      constexpr intptr_t options =
          PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK |
          PTRACE_O_TRACEVFORKDONE | PTRACE_O_TRACECLONE | PTRACE_O_TRACEEXEC |
          PTRACE_O_TRACEEXIT | PTRACE_O_TRACESECCOMP | PTRACE_O_EXITKILL;
      int ret = ptrace(PTRACE_SEIZE, task, 0, options);
      if (ret != 0) {
        if (errno == EPERM) {
          // Sometimes when a task is exiting we can get an EPERM from ptrace.
          // Let's try again up until the timeout in this situation.
          PLOG(WARNING) << "ptrace(PTRACE_SEIZE, " << task << ", "
                        << absl::StrCat("0x", absl::Hex(options))
                        << "), trying again...";
          tasks_left.insert(task);
          continue;
        }
        if (errno == ESRCH) {
          // A task may have exited since we captured the task list, we will
          // allow things to continue after we log a warning.
          PLOG(WARNING)
              << "ptrace(PTRACE_SEIZE, " << task << ", "
              << absl::StrCat("0x", absl::Hex(options))
              << ") skipping exited task. Continuing with other tasks.";
          continue;
        }
        // Any other errno will be considered a failure.
        PLOG(ERROR) << "ptrace(PTRACE_SEIZE, " << task << ", "
                    << absl::StrCat("0x", absl::Hex(options)) << ") failed.";
        return false;
      }
      tasks_attached.insert(task);
    }
    if (!tasks_left.empty()) {
      if (absl::Now() < deadline) {
        LOG(ERROR) << "Attaching to sandboxee timed out: could not attach to "
                   << tasks_left.size() << " tasks";
        return false;
      }
      // Exponential Backoff.
      constexpr absl::Duration kInitialRetry = absl::Milliseconds(1);
      constexpr absl::Duration kMaxRetry = absl::Milliseconds(20);
      const absl::Duration retry_interval =
          kInitialRetry * (1 << std::min(10, retries++));
      absl::SleepFor(
          std::min({retry_interval, kMaxRetry, deadline - absl::Now()}));
    }
    tasks = std::move(tasks_left);
  }

  // Get a list of tasks after attaching.
  if (!sanitizer::GetListOfTasks(pid_, &tasks)) {
    LOG(ERROR) << "Could not get list of tasks";
    return false;
  }

  // Check that we attached to all the threads
  if (tasks_attached != tasks) {
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

void Monitor::ActionProcessSyscall(Regs* regs, const Syscall& syscall) {
  // If the sandboxing is not enabled yet, allow the first __NR_execveat.
  if (syscall.nr() == __NR_execveat && !IsActivelyMonitoring()) {
    VLOG(1) << "[PERMITTED/BEFORE_EXECVEAT]: "
            << "SYSCALL ::: PID: " << regs->pid() << ", PROG: '"
            << util::GetProgName(regs->pid())
            << "' : " << syscall.GetDescription();
    ContinueProcess(regs->pid(), 0);
    return;
  }

  // Notify can decide whether we want to allow this syscall. It could be useful
  // for sandbox setups in which some syscalls might still need some logging,
  // but nonetheless be allowed ('permissible syscalls' in sandbox v1).
  if (notify_->EventSyscallTrap(syscall)) {
    LOG(WARNING) << "[PERMITTED]: SYSCALL ::: PID: " << regs->pid()
                 << ", PROG: '" << util::GetProgName(regs->pid())
                 << "' : " << syscall.GetDescription();

    ContinueProcess(regs->pid(), 0);
    return;
  }

  // TODO(wiktorg): Further clean that up, probably while doing monitor cleanup
  // log_file_ not null iff FLAGS_sandbox2_danger_danger_permit_all_and_log is
  // set.
  if (log_file_) {
    std::string syscall_description = syscall.GetDescription();
    PCHECK(absl::FPrintF(log_file_, "PID: %d %s\n", regs->pid(),
                         syscall_description) >= 0);
    ContinueProcess(regs->pid(), 0);
    return;
  }

  if (absl::GetFlag(FLAGS_sandbox2_danger_danger_permit_all)) {
    ContinueProcess(regs->pid(), 0);
    return;
  }

  ActionProcessSyscallViolation(regs, syscall, kSyscallViolation);
}

void Monitor::ActionProcessSyscallViolation(Regs* regs, const Syscall& syscall,
                                            ViolationType violation_type) {
  LogSyscallViolation(syscall);
  notify_->EventSyscallViolation(syscall, violation_type);
  SetExitStatusCode(Result::VIOLATION, syscall.nr());
  result_.SetSyscall(absl::make_unique<Syscall>(syscall));
  SetAdditionalResultInfo(absl::make_unique<Regs>(*regs));
  // Rewrite the syscall argument to something invalid (-1).
  // The process will be killed anyway so this is just a precaution.
  auto status = regs->SkipSyscallReturnValue(-ENOSYS);
  if (!status.ok()) {
    LOG(ERROR) << status;
  }
}

void Monitor::LogSyscallViolation(const Syscall& syscall) const {
  // Do not unwind libunwind.
  if (executor_->libunwind_sbox_for_pid_ != 0) {
    LOG(ERROR) << "Sandbox violation during execution of libunwind: "
               << syscall.GetDescription();
    return;
  }

  // So, this is an invalid syscall. Will be killed by seccomp-bpf policies as
  // well, but we should be on a safe side here as well.
  LOG(ERROR) << "SANDBOX VIOLATION : PID: " << syscall.pid() << ", PROG: '"
             << util::GetProgName(syscall.pid())
             << "' : " << syscall.GetDescription();
  if (VLOG_IS_ON(1)) {
    VLOG(1) << "Cmdline: " << util::GetCmdLine(syscall.pid());
    VLOG(1) << "Task Name: " << util::GetProcStatusLine(syscall.pid(), "Name");
    VLOG(1) << "Tgid: " << util::GetProcStatusLine(syscall.pid(), "Tgid");
  }

  LogSyscallViolationExplanation(syscall);
}

void Monitor::EventPtraceSeccomp(pid_t pid, int event_msg) {
  // If the seccomp-policy is using RET_TRACE, we request that it returns the
  // syscall architecture identifier in the SECCOMP_RET_DATA.
  const auto syscall_arch = static_cast<sapi::cpu::Architecture>(event_msg);
  Regs regs(pid);
  auto status = regs.Fetch();
  if (!status.ok()) {
    LOG(ERROR) << status;
    SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_FETCH);
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
  ContinueProcess(pid, 0);
}

void Monitor::EventPtraceExit(pid_t pid, int event_msg) {
  // A regular exit, let it continue (fast-path).
  if (WIFEXITED(event_msg)) {
    ContinueProcess(pid, 0);
    return;
  }

  // Fetch the registers as we'll need them to fill the result in any case
  auto regs = absl::make_unique<Regs>(pid);
  auto status = regs->Fetch();
  if (!status.ok()) {
    LOG(ERROR) << "failed to fetch regs: " << status;
    SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_FETCH);
    return;
  }

  // Process signaled due to seccomp violation.
  if (WIFSIGNALED(event_msg) && WTERMSIG(event_msg) == SIGSYS) {
    VLOG(1) << "PID: " << pid << " violation uncovered via the EXIT_EVENT";
    ActionProcessSyscallViolation(
        regs.get(), regs->ToSyscall(Syscall::GetHostArch()), kSyscallViolation);
    return;
  }

  // This can be reached in three cases:
  // 1) Process was killed from the sandbox.
  // 2) Process was killed because it hit a timeout.
  // 3) Regular signal/other exit cause.
  if (pid == pid_) {
    VLOG(1) << "PID: " << pid << " main special exit";
    if (network_violation_) {
      SetExitStatusCode(Result::VIOLATION, Result::VIOLATION_NETWORK);
      result_.SetNetworkViolation(network_proxy_server_->violation_msg_);
    } else if (external_kill_) {
      SetExitStatusCode(Result::EXTERNAL_KILL, 0);
    } else if (timed_out_) {
      SetExitStatusCode(Result::TIMEOUT, 0);
    } else {
      SetExitStatusCode(Result::SIGNALED, WTERMSIG(event_msg));
    }
    SetAdditionalResultInfo(std::move(regs));
  }
  VLOG(1) << "Continuing";
  ContinueProcess(pid, 0);
}

void Monitor::EventPtraceStop(pid_t pid, int stopsig) {
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

void Monitor::StateProcessStopped(pid_t pid, int status) {
  int stopsig = WSTOPSIG(status);
  if (__WPTRACEEVENT(status) == 0) {
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

  if (ABSL_PREDICT_FALSE(pid == pid_ && should_dump_stack_ &&
                         executor_->libunwind_sbox_for_pid_ == 0 &&
                         policy_->GetNamespace())) {
    auto stack_trace = [this,
                        pid]() -> absl::StatusOr<std::vector<std::string>> {
      Regs regs(pid);
      SAPI_RETURN_IF_ERROR(regs.Fetch());
      return GetStackTrace(&regs, policy_->GetNamespace()->mounts());
    }();

    if (!stack_trace.ok()) {
      LOG(WARNING) << "FAILED TO GET SANDBOX STACK : " << stack_trace.status();
    } else if (SAPI_VLOG_IS_ON(0)) {
      VLOG(0) << "SANDBOX STACK: PID: " << pid << ", [";
      for (const auto& frame : *stack_trace) {
        VLOG(0) << "  " << frame;
      }
      VLOG(0) << "]";
    }
    should_dump_stack_ = false;
  }

#ifndef PTRACE_EVENT_STOP
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

void Monitor::LogSyscallViolationExplanation(const Syscall& syscall) const {
  const uintptr_t syscall_nr = syscall.nr();
  const uintptr_t arg0 = syscall.args()[0];

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

void Monitor::EnableNetworkProxyServer() {
  int fd = ipc_->ReceiveFd(NetworkProxyClient::kFDName);

  network_proxy_server_ = absl::make_unique<NetworkProxyServer>(
      fd, &policy_->allowed_hosts_.value(), pthread_self());

  network_proxy_thread_ = std::thread(&NetworkProxyServer::Run,
  network_proxy_server_.get());
}

}  // namespace sandbox2
