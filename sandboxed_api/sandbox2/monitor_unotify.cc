#include "sandboxed_api/sandbox2/monitor_unotify.h"

#include <linux/seccomp.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "sandboxed_api/sandbox2/bpf_evaluator.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/flags.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/sandbox2/monitor_base.h"
#include "sandboxed_api/sandbox2/notify.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sanitizer.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/sandbox2/util/seccomp_unotify.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/status_macros.h"
#include "sandboxed_api/util/thread.h"

#define DO_USER_NOTIF BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_USER_NOTIF)

namespace sandbox2 {

namespace {

using ::sapi::file_util::fileops::FDCloser;

absl::Status WaitForFdReadable(int fd, absl::Time deadline) {
  pollfd pfds[] = {
      {.fd = fd, .events = POLLIN},
  };
  for (absl::Duration remaining = deadline - absl::Now();
       remaining > absl::ZeroDuration(); remaining = deadline - absl::Now()) {
    int ret = poll(pfds, ABSL_ARRAYSIZE(pfds),
                   static_cast<int>(absl::ToInt64Milliseconds(remaining)));
    if (ret > 0) {
      if (pfds[0].revents & POLLIN) {
        return absl::OkStatus();
      }
      if (pfds[0].revents & POLLHUP) {
        return absl::UnavailableError("hangup");
      }
      return absl::InternalError("poll");
    }
    if (ret == -1 && errno != EINTR) {
      return absl::ErrnoToStatus(errno, "poll");
    }
  }
  return absl::DeadlineExceededError("waiting for fd");
}

absl::Status ReadWholeWithDeadline(int fd, std::vector<iovec> vecs_vec,
                                   absl::Time deadline) {
  absl::Span<iovec> vecs = absl::MakeSpan(vecs_vec);
  while (!vecs.empty()) {
    SAPI_RETURN_IF_ERROR(WaitForFdReadable(fd, deadline));
    ssize_t r = readv(fd, vecs.data(), vecs.size());
    if (r < 0 && errno != EINTR) {
      return absl::ErrnoToStatus(errno, "readv");
    }
    while (r > 0) {
      if (vecs.empty()) {
        return absl::InternalError("readv return value too big");
      }
      iovec& vec = vecs.front();
      if (r < vec.iov_len) {
        vec.iov_len -= r;
        vec.iov_base = reinterpret_cast<char*>(vec.iov_base) + r;
        break;
      }
      r -= vec.iov_len;
      vecs.remove_prefix(1);
    }
  }
  return absl::OkStatus();
}

// Waits for the given task to stop. Returns an error if the task is not stopped
// within the given timeout.
absl::Status WaitForTaskToStop(pid_t pid) {
  int wstatus = 0;
  int ret = 0;
  while (ret == 0) {
    ret = waitpid(pid, &wstatus, __WNOTHREAD | __WALL | WUNTRACED | WNOHANG);
  }
  if (ret == -1) {
    return absl::ErrnoToStatus(errno,
                               absl::StrCat("waiting for stop, task = ", pid));
  }
  return WIFSTOPPED(wstatus) ? absl::OkStatus()
                             : absl::InternalError("task did not stop");
}

}  // namespace

UnotifyMonitor::UnotifyMonitor(Executor* executor, Policy* policy,
                               Notify* notify)
    : MonitorBase(executor, policy, notify) {
  type_ = FORKSERVER_MONITOR_UNOTIFY;
  if (executor_->limits()->wall_time_limit() != absl::ZeroDuration()) {
    auto deadline = absl::Now() + executor_->limits()->wall_time_limit();
    deadline_millis_.store(absl::ToUnixMillis(deadline),
                           std::memory_order_relaxed);
  }
  external_kill_request_flag_.test_and_set(std::memory_order_relaxed);
  dump_stack_request_flag_.test_and_set(std::memory_order_relaxed);
}

void UnotifyMonitor::RunInternal() {
  thread_ = sapi::Thread(this, &UnotifyMonitor::Run, "sandbox2-Monitor");

  // Wait for the Monitor to set-up the sandboxee correctly (or fail while
  // doing that). From here on, it is safe to use the IPC object for
  // non-sandbox-related data exchange.
  setup_notification_.WaitForNotification();
}

absl::Status UnotifyMonitor::SendPolicy(
    const std::vector<sock_filter>& policy) {
  original_policy_ = policy;
  std::vector<sock_filter> modified_policy = policy;
  const sock_filter trace_action = SANDBOX2_TRACE;
  for (sock_filter& filter : modified_policy) {
    if ((filter.code == BPF_RET + BPF_K && filter.k == SECCOMP_RET_KILL) ||
        (filter.code == trace_action.code && filter.k == trace_action.k)) {
      filter = DO_USER_NOTIF;
    }
  }
  return MonitorBase::SendPolicy(modified_policy);
}

void UnotifyMonitor::HandleViolation(const Syscall& syscall) {
  ViolationType violation_type = syscall.arch() == Syscall::GetHostArch()
                                     ? ViolationType::kSyscall
                                     : ViolationType::kArchitectureSwitch;
  LogSyscallViolation(syscall);
  notify_->EventSyscallViolation(syscall, violation_type);
  MaybeGetStackTrace(syscall.pid(), Result::VIOLATION);
  SetExitStatusCode(Result::VIOLATION, syscall.nr());
  result_.SetSyscall(std::make_unique<Syscall>(syscall));
  KillSandboxee();
}

void UnotifyMonitor::AllowSyscallViaUnotify(seccomp_notif req) {
  if (!util::SeccompUnotify::IsContinueSupported()) {
    LOG(ERROR)
        << "SECCOMP_USER_NOTIF_FLAG_CONTINUE not supported by the kernel.";
    SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_NOTIFY);
    return;
  }
  if (absl::Status status = seccomp_unotify_.RespondContinue(req);
      !status.ok()) {
    if (absl::IsNotFound(status)) {
      VLOG(1) << "Unotify send failed with ENOENT";
    } else {
      SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_NOTIFY);
    }
  }
}

void UnotifyMonitor::HandleUnotify() {
  absl::StatusOr<seccomp_notif> req_data = seccomp_unotify_.Receive();
  if (!req_data.ok()) {
    if (absl::IsNotFound(req_data.status())) {
      VLOG(1) << "Unotify recv failed with ENOENT";
    } else {
      SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_NOTIFY);
      return;
    }
  }
  Syscall syscall(req_data->pid, req_data->data);
  if (wait_for_execveat() && syscall.nr() == __NR_execveat &&
      util::SeccompUnotify::IsContinueSupported()) {
    VLOG(1) << "[PERMITTED/BEFORE_EXECVEAT]: " << "SYSCALL ::: PID: "
            << syscall.pid() << ", PROG: '" << util::GetProgName(syscall.pid())
            << "' : " << syscall.GetDescription();
    set_wait_for_execveat(false);
    AllowSyscallViaUnotify(*req_data);
    return;
  }
  absl::StatusOr<uint32_t> policy_ret =
      bpf::Evaluate(original_policy_, req_data->data);
  if (!policy_ret.ok()) {
    LOG(ERROR) << "Failed to evaluate policy: " << policy_ret.status();
    SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_NOTIFY);
  }

  if (absl::GetFlag(FLAGS_sandbox2_danger_danger_permit_all) || log_file_) {
    std::string syscall_description = syscall.GetDescription();
    if (log_file_) {
      PCHECK(absl::FPrintF(log_file_, "PID: %d %s\n", syscall.pid(),
                           syscall_description) >= 0);
    }
    VLOG(1) << "PID: " << syscall.pid() << " " << syscall_description;
    AllowSyscallViaUnotify(*req_data);
    return;
  }

  const sock_filter trace_action = SANDBOX2_TRACE;
  bool should_trace = *policy_ret == trace_action.k;
  Notify::TraceAction trace_response = Notify::TraceAction::kDeny;
  if (should_trace) {
    trace_response = notify_->EventSyscallTrace(syscall);
  }
  switch (trace_response) {
    case Notify::TraceAction::kAllow:
      AllowSyscallViaUnotify(*req_data);
      return;
    case Notify::TraceAction::kDeny:
      HandleViolation(syscall);
      return;
    case Notify::TraceAction::kInspectAfterReturn:
      LOG(FATAL) << "TraceAction::kInspectAfterReturn not supported by unotify "
                    "monitor";
    default:
      LOG(FATAL) << "Unknown TraceAction: " << static_cast<int>(trace_response);
  }
}

void UnotifyMonitor::Run() {
  absl::Cleanup monitor_done = [this] {
    getrusage(RUSAGE_THREAD, result_.GetRUsageMonitor());
    OnDone();
  };

  absl::Cleanup setup_notify = [this] { setup_notification_.Notify(); };
  if (!InitSetupUnotify()) {
    SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_NOTIFY);
    return;
  }
  if (!InitSetupNotifyEventFd()) {
    SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_NOTIFY);
    return;
  }

  std::move(setup_notify).Invoke();

  pollfd pfds[] = {
      {.fd = process_.status_fd.get(), .events = POLLIN},
      {.fd = seccomp_unotify_.GetFd(), .events = POLLIN},
      {.fd = monitor_notify_fd_.get(), .events = POLLIN},
  };
  while (result_.final_status() == Result::UNSET) {
    int64_t deadline = deadline_millis_.load(std::memory_order_relaxed);
    absl::Duration remaining = absl::FromUnixMillis(deadline) - absl::Now();
    if (deadline != 0 && remaining <= absl::ZeroDuration()) {
      VLOG(1) << "Sandbox process hit timeout due to the walltime timer";
      timed_out_ = true;
      MaybeGetStackTrace(process_.main_pid, Result::TIMEOUT);
      KillSandboxee();
      SetExitStatusFromStatusPipe();
      break;
    }

    if (!external_kill_request_flag_.test_and_set(std::memory_order_relaxed)) {
      external_kill_ = true;
      MaybeGetStackTrace(process_.main_pid, Result::EXTERNAL_KILL);
      KillSandboxee();
      SetExitStatusFromStatusPipe();
      break;
    }

    if (network_proxy_server_ &&
        network_proxy_server_->violation_occurred_.load(
            std::memory_order_acquire) &&
        !network_violation_) {
      network_violation_ = true;
      MaybeGetStackTrace(process_.main_pid, Result::VIOLATION);
      KillSandboxee();
      SetExitStatusFromStatusPipe();
      break;
    }
    constexpr int64_t kMinWakeupMsec = 30000;
    int timeout_msec = kMinWakeupMsec;
    if (remaining > absl::ZeroDuration()) {
      timeout_msec = static_cast<int>(
          std::min(kMinWakeupMsec, absl::ToInt64Milliseconds(remaining)));
    }
    int ret = poll(pfds, ABSL_ARRAYSIZE(pfds), timeout_msec);
    if (ret == 0 || (ret == -1 && errno == EINTR)) {
      continue;
    }
    if (ret == -1) {
      PLOG(ERROR) << "waiting for action failed";
      SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_MONITOR);
      break;
    }
    if (pfds[2].revents & POLLIN) {
      uint64_t value = 0;
      (void)read(monitor_notify_fd_.get(), &value, sizeof(value));
      continue;
    }
    if (pfds[0].revents & POLLIN) {
      SetExitStatusFromStatusPipe();
      break;
    }
    if (pfds[0].revents & POLLHUP) {
      LOG(ERROR) << "Status pipe hangup";
      SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_MONITOR);
      break;
    }
    if (pfds[1].revents & POLLIN) {
      HandleUnotify();
    }
  }
  KillInit();
}

void UnotifyMonitor::SetExitStatusFromStatusPipe() {
  int code, status;
  rusage usage;

  std::vector<iovec> iov = {
      {.iov_base = &code, .iov_len = sizeof(code)},
      {.iov_base = &status, .iov_len = sizeof(status)},
      {.iov_base = &usage, .iov_len = sizeof(usage)},
  };

  if (absl::Status status = ReadWholeWithDeadline(
          process_.status_fd.get(), iov, absl::Now() + absl::Seconds(1));
      !status.ok()) {
    PLOG(ERROR) << "reading status pipe failed " << status;
    SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_MONITOR);
    return;
  }

  result_.SetRUsageSandboxee(usage);
  if (code == CLD_EXITED) {
    SetExitStatusCode(Result::OK, status);
  } else if (code == CLD_KILLED || code == CLD_DUMPED) {
    if (network_violation_) {
      SetExitStatusCode(Result::VIOLATION, Result::VIOLATION_NETWORK);
      result_.SetNetworkViolation(network_proxy_server_->violation_msg_);
    } else if (external_kill_) {
      SetExitStatusCode(Result::EXTERNAL_KILL, 0);
    } else if (timed_out_) {
      SetExitStatusCode(Result::TIMEOUT, 0);
    } else {
      SetExitStatusCode(Result::SIGNALED, status);
    }
  } else {
    LOG(ERROR) << "Unexpected exit code: " << code;
    SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_MONITOR);
  }
}

bool UnotifyMonitor::InitSetupUnotify() {
  if (!SendMonitorReadyMessageAndFlags(Client::kSandbox2ClientUnotify)) {
    LOG(ERROR) << "Couldn't send Client::kSandbox2ClientUnotify message";
    return false;
  }
  int fd;
  if (!comms_->RecvFD(&fd)) {
    LOG(ERROR) << "Couldn't recv unotify fd";
    return false;
  }
  if (absl::Status status = seccomp_unotify_.Init(FDCloser(fd)); !status.ok()) {
    LOG(ERROR) << "Could not init seccomp_unotify: " << status;
    return false;
  }
  return true;
}

bool UnotifyMonitor::InitSetupNotifyEventFd() {
  int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (fd == -1) {
    PLOG(ERROR) << "failed creating monitor pipe";
    return false;
  }
  monitor_notify_fd_ = FDCloser(fd);
  return true;
}

void UnotifyMonitor::NotifyMonitor() {
  absl::ReaderMutexLock lock(notify_mutex_);
  if (monitor_notify_fd_.get() < 0) {
    return;
  }
  uint64_t value = 1;
  PCHECK(write(monitor_notify_fd_.get(), &value, sizeof(value)) ==
         sizeof(value));
}

bool UnotifyMonitor::KillSandboxee() {
  VLOG(1) << "Sending SIGKILL to the PID: " << process_.main_pid;
  if (kill(process_.main_pid, SIGKILL) != 0) {
    PLOG(ERROR) << "Could not send SIGKILL to PID " << process_.main_pid;
    return false;
  }
  return true;
}

void UnotifyMonitor::KillInit() {
  VLOG(1) << "Sending SIGKILL to the PID: " << process_.init_pid;
  if (kill(process_.init_pid, SIGKILL) != 0) {
    PLOG(ERROR) << "Could not send SIGKILL to PID " << process_.init_pid;
  }
}

void UnotifyMonitor::Join() {
  absl::MutexLock lock(notify_mutex_);
  if (thread_.IsJoinable()) {
    thread_.Join();
    CHECK(IsDone()) << "Monitor did not terminate";
    VLOG(1) << "Final execution status: " << result_.ToString();
    CHECK(result_.final_status() != Result::UNSET);
    monitor_notify_fd_.Close();
  }
}

void UnotifyMonitor::MaybeGetStackTrace(pid_t pid, Result::StatusEnum status) {
  if (!ShouldCollectStackTrace(status)) {
    return;
  }
  auto stack_trace = GetStackTrace(pid);
  if (!stack_trace.ok()) {
    LOG(ERROR) << "Getting stack trace: " << stack_trace.status();
    return;
  }
  result_.set_stack_trace(*stack_trace);
  if (!policy_->collect_all_threads_stacktrace()) {
    return;
  }
  auto stack_traces = GetThreadStackTraces(pid);
  if (!stack_traces.ok()) {
    LOG(ERROR) << "Getting stack traces: " << stack_traces.status();
    return;
  }
  // Put the violating thread's stack trace at the front
  stack_traces->insert(stack_traces->begin(), {pid, std::move(*stack_trace)});
  result_.set_thread_stack_trace(*std::move(stack_traces));
}

absl::StatusOr<std::vector<std::pair<pid_t, std::vector<std::string>>>>
UnotifyMonitor::GetThreadStackTraces(pid_t pid) {
  SAPI_ASSIGN_OR_RETURN(absl::flat_hash_set<int> tasks,
                        sanitizer::GetListOfTasks(pid));
  tasks.erase(pid);

  std::vector<pid_t> attached_tasks;
  absl::Cleanup cleanup = [&attached_tasks] {
    for (pid_t task : attached_tasks) {
      if (ptrace(PTRACE_DETACH, task, 0, 0) != 0) {
        LOG(ERROR) << "Could not detach from pid = " << task;
      }
    }
  };

  for (pid_t task : tasks) {
    if (ptrace(PTRACE_ATTACH, task, 0, 0) != 0) {
      LOG(ERROR) << "Could not attach to pid = " << task;
      continue;
    }
    attached_tasks.push_back(task);
  }

  std::vector<std::pair<pid_t, std::vector<std::string>>> thread_stack_traces;
  for (pid_t task : attached_tasks) {
    Regs regs(task);
    absl::Status status = regs.Fetch();
    if (!status.ok()) {
      LOG(ERROR) << "Failed to fetch regs: " << status;
      continue;
    }
    auto stack = GetAndLogStackTrace(&regs);
    if (!stack.ok()) {
      LOG_IF(ERROR,
             absl::GetFlag(FLAGS_sandbox2_log_unobtainable_stack_traces_errors))
          << "Could not obtain stack trace: " << stack.status();
      continue;
    }
    thread_stack_traces.push_back({task, std::move(*stack)});
  }

  return thread_stack_traces;
}

absl::StatusOr<std::vector<std::string>> UnotifyMonitor::GetStackTrace(
    pid_t pid) {
  if (ptrace(PTRACE_ATTACH, pid, 0, 0) != 0) {
    return absl::ErrnoToStatus(errno,
                               absl::StrCat("could not attach to pid = ", pid));
  }

  SAPI_RETURN_IF_ERROR(WaitForTaskToStop(pid));

  absl::Cleanup cleanup = [pid] {
    if (ptrace(PTRACE_DETACH, pid, 0, 0) != 0) {
      LOG(ERROR) << "Could not detach after obtaining stack trace from pid = "
                 << pid;
    }
  };
  Regs regs(pid);
  absl::Status status = regs.Fetch();
  if (!status.ok()) {
    if (absl::IsNotFound(status)) {
      LOG(WARNING) << "failed to fetch regs: " << status;
      return status;
    }
    SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_FETCH);
    return status;
  }
  return GetAndLogStackTrace(&regs);
}

}  // namespace sandbox2
