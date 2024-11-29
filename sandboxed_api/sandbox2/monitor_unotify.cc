#include "sandboxed_api/sandbox2/monitor_unotify.h"

#include <linux/audit.h>
#include <linux/seccomp.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
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
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/bpf_evaluator.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/sandbox2/monitor_base.h"
#include "sandboxed_api/sandbox2/notify.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/status_macros.h"
#include "sandboxed_api/util/thread.h"

#ifndef SECCOMP_RET_USER_NOTIF
#define SECCOMP_RET_USER_NOTIF 0x7fc00000U /* notifies userspace */
#endif

#ifndef SECCOMP_USER_NOTIF_FLAG_CONTINUE
#define SECCOMP_USER_NOTIF_FLAG_CONTINUE 1
#endif

#define DO_USER_NOTIF BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_USER_NOTIF)

#ifndef SECCOMP_GET_NOTIF_SIZES
#define SECCOMP_GET_NOTIF_SIZES 3

struct seccomp_notif_sizes {
  __u16 seccomp_notif;
  __u16 seccomp_notif_resp;
  __u16 seccomp_data;
};
#endif

#ifndef SECCOMP_IOCTL_NOTIF_RECV
#ifndef SECCOMP_IOWR
#define SECCOMP_IOC_MAGIC '!'
#define SECCOMP_IO(nr) _IO(SECCOMP_IOC_MAGIC, nr)
#define SECCOMP_IOWR(nr, type) _IOWR(SECCOMP_IOC_MAGIC, nr, type)
#endif

// Flags for seccomp notification fd ioctl.
#define SECCOMP_IOCTL_NOTIF_RECV SECCOMP_IOWR(0, struct seccomp_notif)
#define SECCOMP_IOCTL_NOTIF_SEND SECCOMP_IOWR(1, struct seccomp_notif_resp)
#endif

namespace sandbox2 {

namespace {

using ::sapi::file_util::fileops::FDCloser;

int seccomp(unsigned int operation, unsigned int flags, void* args) {
  return syscall(SYS_seccomp, operation, flags, args);
}

sapi::cpu::Architecture AuditArchToCPUArch(uint32_t arch) {
  switch (arch) {
    case AUDIT_ARCH_AARCH64:
      return sapi::cpu::Architecture::kArm64;
    case AUDIT_ARCH_ARM:
      return sapi::cpu::Architecture::kArm;
    case AUDIT_ARCH_X86_64:
      return sapi::cpu::Architecture::kX8664;
    case AUDIT_ARCH_I386:
      return sapi::cpu::Architecture::kX86;
    case AUDIT_ARCH_PPC64LE:
      return sapi::cpu::Architecture::kPPC64LE;
    default:
      return sapi::cpu::Architecture::kUnknown;
  }
}

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
  MaybeGetStackTrace(req_->pid, Result::VIOLATION);
  SetExitStatusCode(Result::VIOLATION, syscall.nr());
  notify_->EventSyscallViolation(syscall, violation_type);
  result_.SetSyscall(std::make_unique<Syscall>(syscall));
  KillSandboxee();
}

void UnotifyMonitor::AllowSyscallViaUnotify() {
  memset(resp_.get(), 0, resp_size_);
  resp_->id = req_->id;
  resp_->val = 0;
  resp_->error = 0;
  resp_->flags = SECCOMP_USER_NOTIF_FLAG_CONTINUE;
  if (ioctl(seccomp_notify_fd_.get(), SECCOMP_IOCTL_NOTIF_SEND, resp_.get()) !=
      0) {
    if (errno == ENOENT) {
      VLOG(1) << "Unotify send failed with ENOENT";
    } else {
      LOG_IF(ERROR, errno == EINVAL)
          << "Unotify send failed with EINVAL. Likely "
             "SECCOMP_USER_NOTIF_FLAG_CONTINUE unsupported by the kernel.";
      SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_NOTIFY);
    }
  }
}

void UnotifyMonitor::HandleUnotify() {
  memset(req_.get(), 0, req_size_);
  if (ioctl(seccomp_notify_fd_.get(), SECCOMP_IOCTL_NOTIF_RECV, req_.get()) !=
      0) {
    if (errno == ENOENT) {
      VLOG(1) << "Unotify recv failed with ENOENT";
    } else {
      SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_NOTIFY);
    }
    return;
  }
  Syscall syscall(AuditArchToCPUArch(req_->data.arch), req_->data.nr,
                  {req_->data.args[0], req_->data.args[1], req_->data.args[2],
                   req_->data.args[3], req_->data.args[4], req_->data.args[5]},
                  req_->pid, 0, req_->data.instruction_pointer);
  absl::StatusOr<uint32_t> policy_ret =
      bpf::Evaluate(original_policy_, req_->data);
  if (!policy_ret.ok()) {
    LOG(ERROR) << "Failed to evaluate policy: " << policy_ret.status();
    SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_NOTIFY);
  }
  const sock_filter trace_action = SANDBOX2_TRACE;
  bool should_trace = *policy_ret == trace_action.k;
  Notify::TraceAction trace_response = Notify::TraceAction::kDeny;
  if (should_trace) {
    trace_response = notify_->EventSyscallTrace(syscall);
  }
  switch (trace_response) {
    case Notify::TraceAction::kAllow:
      AllowSyscallViaUnotify();
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
      {.fd = seccomp_notify_fd_.get(), .events = POLLIN},
      {.fd = monitor_notify_fd_.get(), .events = POLLIN},
  };
  while (result_.final_status() == Result::UNSET) {
    int64_t deadline = deadline_millis_.load(std::memory_order_relaxed);
    absl::Duration remaining = absl::FromUnixMillis(deadline) - absl::Now();
    if (deadline != 0 && remaining < absl::ZeroDuration()) {
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
    SetExitStatusCode(Result::INTERNAL_ERROR, Result::FAILED_MONITOR);
  }
}

bool UnotifyMonitor::InitSetupUnotify() {
  if (!comms_->SendUint32(Client::kSandbox2ClientUnotify)) {
    LOG(ERROR) << "Couldn't send Client::kSandbox2ClientUnotify message";
    return false;
  }
  int fd;
  if (!comms_->RecvFD(&fd)) {
    LOG(ERROR) << "Couldn't recv unotify fd";
    return false;
  }
  seccomp_notify_fd_ = FDCloser(fd);
  struct seccomp_notif_sizes sizes = {};
  if (seccomp(SECCOMP_GET_NOTIF_SIZES, 0, &sizes) == -1) {
    LOG(ERROR) << "Couldn't get seccomp_notif_sizes";
    return false;
  }
  req_size_ = sizes.seccomp_notif;
  req_.reset(static_cast<seccomp_notif*>(malloc(req_size_)));
  resp_size_ = sizes.seccomp_notif_resp;
  resp_.reset(static_cast<seccomp_notif_resp*>(malloc(resp_size_)));
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
  absl::ReaderMutexLock lock(&notify_mutex_);
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
  absl::MutexLock lock(&notify_mutex_);
  if (thread_.IsJoinable()) {
    thread_.Join();
    CHECK(IsDone()) << "Monitor did not terminate";
    VLOG(1) << "Final execution status: " << result_.ToString();
    CHECK(result_.final_status() != Result::UNSET);
    monitor_notify_fd_.Close();
  }
}

void UnotifyMonitor::MaybeGetStackTrace(pid_t pid, Result::StatusEnum status) {
  if (ShouldCollectStackTrace(status)) {
    auto stack = GetStackTrace(pid);
    if (stack.ok()) {
      result_.set_stack_trace(*stack);
    } else {
      LOG(ERROR) << "Getting stack trace: " << stack.status();
    }
  }
}

absl::StatusOr<std::vector<std::string>> UnotifyMonitor::GetStackTrace(
    pid_t pid) {
  if (ptrace(PTRACE_ATTACH, pid, 0, 0) != 0) {
    return absl::ErrnoToStatus(errno,
                               absl::StrCat("could not attach to pid = ", pid));
  }
  int wstatus = 0;
  while (!WIFSTOPPED(wstatus)) {
    pid_t ret =
        waitpid(pid, &wstatus, __WNOTHREAD | __WALL | WUNTRACED | WNOHANG);
    if (ret == -1) {
      return absl::ErrnoToStatus(errno,
                                 absl::StrCat("waiting for stop, pid = ", pid));
    }
  }
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
