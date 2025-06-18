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

// Implementation file for the sandbox2::MonitorBase class.

#include "sandboxed_api/sandbox2/monitor_base.h"

#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/resource.h>
#include <syscall.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/log/vlog_is_on.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/flags.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/sandbox2/limits.h"
#include "sandboxed_api/sandbox2/mounts.h"
#include "sandboxed_api/sandbox2/namespace.h"
#include "sandboxed_api/sandbox2/network_proxy/client.h"
#include "sandboxed_api/sandbox2/network_proxy/server.h"
#include "sandboxed_api/sandbox2/notify.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/stack_trace.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/util/file_helpers.h"
#include "sandboxed_api/util/strerror.h"
#include "sandboxed_api/util/temp_file.h"
#include "sandboxed_api/util/thread.h"

namespace sandbox2 {
namespace {

void MaybeEnableTomoyoLsmWorkaround(Mounts& mounts, std::string& comms_fd_dev) {
  static auto tomoyo_active = []() -> bool {
    std::string lsm_list;
    if (auto status = sapi::file::GetContents(
            "/sys/kernel/security/lsm", &lsm_list, sapi::file::Defaults());
        !status.ok() && !absl::IsNotFound(status)) {
      VLOG(1) << "Checking active LSMs failed: " << status.message() << ": "
              << sapi::StrError(errno);
      return false;
    }
    return absl::StrContains(lsm_list, "tomoyo");
  }();

  if (!tomoyo_active) {
    return;
  }
  VLOG(1) << "Tomoyo LSM active, enabling workaround";

  if (mounts.ResolvePath("/dev").ok() || mounts.ResolvePath("/dev/fd").ok()) {
    // Avoid shadowing /dev/fd/1022 below if /dev or /dev/fd is already mapped.
    VLOG(1) << "Parent dir already mapped, skipping";
    return;
  }

  auto temp_file = sapi::CreateNamedTempFileAndClose("/tmp/");
  if (!temp_file.ok()) {
    LOG(WARNING) << "Failed to create empty temp file: " << temp_file.status();
    return;
  }
  comms_fd_dev = std::move(*temp_file);

  // Ignore errors here, as the file itself might already be mapped.
  if (auto status = mounts.AddFileAt(
          comms_fd_dev, absl::StrCat("/dev/fd/", Comms::kSandbox2TargetExecFD),
          false);
      !status.ok()) {
    VLOG(1) << "Mapping comms FD: %s" << status.message();
  }
}

void LogContainer(const std::vector<std::string>& container) {
  for (size_t i = 0; i < container.size(); ++i) {
    LOG(INFO) << "[" << std::setfill('0') << std::setw(4) << i
              << "]=" << container[i];
  }
}

}  // namespace

MonitorBase::MonitorBase(Executor* executor, Policy* policy, Notify* notify)
    : executor_(executor),
      policy_(policy),
      notify_(notify),
      // NOLINTNEXTLINE clang-diagnostic-deprecated-declarations
      comms_(executor_->ipc()->comms()),
      ipc_(executor_->ipc()),
      uses_custom_forkserver_(executor_->fork_client_ != nullptr) {
  wait_for_execveat_ = executor->enable_sandboxing_pre_execve_;
  // It's a pre-connected Comms channel, no need to accept new connection.
  CHECK(comms_->IsConnected());
  std::string path =
      absl::GetFlag(FLAGS_sandbox2_danger_danger_permit_all_and_log);
  if (!path.empty()) {
    log_file_ = std::fopen(path.c_str(), "a+");
    PCHECK(log_file_ != nullptr) << "Failed to open log file '" << path << "'";
  }

  if (auto& ns = policy_->namespace_; ns) {
    // Check for the Tomoyo LSM, which is active by default in several common
    // distribution kernels (esp. Debian).
    MaybeEnableTomoyoLsmWorkaround(ns->mounts(), comms_fd_dev_);
  }
}

MonitorBase::~MonitorBase() {
  if (!comms_fd_dev_.empty()) {
    std::remove(comms_fd_dev_.c_str());
  }
  if (log_file_) {
    std::fclose(log_file_);
  }
  if (network_proxy_thread_.IsJoinable()) {
    network_proxy_thread_.Join();
  }
}

void MonitorBase::OnDone() {
  if (done_notification_.HasBeenNotified()) {
    return;
  }

  notify_->EventFinished(result_);
  ipc_->InternalCleanupFdMap();
  done_notification_.Notify();
}

void MonitorBase::Launch() {

  absl::Cleanup process_cleanup = [this] {
    if (process_.init_pid > 0) {
      kill(process_.init_pid, SIGKILL);
    } else if (process_.main_pid > 0) {
      kill(process_.main_pid, SIGKILL);
    }
  };
  absl::Cleanup monitor_done = [this] { OnDone(); };

  const Namespace* ns = policy_->GetNamespaceOrNull();
  if (VLOG_IS_ON(1) && ns != nullptr) {
    std::vector<std::string> outside_entries;
    std::vector<std::string> inside_entries;
    ns->mounts().RecursivelyListMounts(
        /*outside_entries=*/&outside_entries,
        /*inside_entries=*/&inside_entries);
    VLOG(1) << "Outside entries mapped to chroot:";
    LogContainer(outside_entries);
    VLOG(1) << "Inside entries as they appear in chroot:";
    LogContainer(inside_entries);
  }

  // Don't trace the child: it will allow to use 'strace -f' with the whole
  // sandbox master/monitor, which ptrace_attach'es to the child.
  int clone_flags = CLONE_UNTRACED;

  if (policy_->allowed_hosts_) {
    EnableNetworkProxyServer();
  }

  // Get PID of the sandboxee.
  bool should_have_init = ns && (ns->clone_flags() & CLONE_NEWPID);
  absl::StatusOr<SandboxeeProcess> process = executor_->StartSubProcess(
      clone_flags, ns, policy_->allow_speculation_, type_);

  if (!process.ok()) {
    LOG(ERROR) << "Starting sandboxed subprocess failed: " << process.status();
    SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_SUBPROCESS);
    return;
  }

  process_ = *std::move(process);

  if (process_.main_pid <= 0 || (should_have_init && process_.init_pid <= 0)) {
    SetExitStatusCode(Result::SETUP_ERROR, Result::FAILED_SUBPROCESS);
    return;
  }

  if (!notify_->EventStarted(process_.main_pid, comms_)) {
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
  std::move(process_cleanup).Cancel();

  RunInternal();
  std::move(monitor_done).Cancel();
}

absl::StatusOr<Result> MonitorBase::AwaitResultWithTimeout(
    absl::Duration timeout) {
  auto done = done_notification_.WaitForNotificationWithTimeout(timeout);
  if (!done) {
    return absl::DeadlineExceededError("Sandbox did not finish within timeout");
  }

  Join();
  return result_;
}

void MonitorBase::SetExitStatusCode(Result::StatusEnum final_status,
                                    uintptr_t reason_code) {
  CHECK(result_.final_status() == Result::UNSET);
  result_.SetExitStatusCode(final_status, reason_code);
}

absl::Status MonitorBase::SendPolicy(const std::vector<sock_filter>& policy) {
  if (!comms_->SendBytes(reinterpret_cast<const uint8_t*>(policy.data()),
                         policy.size() * sizeof(sock_filter))) {
    return absl::InternalError("Error while sending policy via comms");
  }
  return absl::OkStatus();
}

bool MonitorBase::SendMonitorReadyMessageAndFlags(uint32_t monitor_type) {
  uint32_t message = monitor_type;
  if (policy_->allow_speculation_) {
    message |= Client::kAllowSpeculationBit;
  }
  return comms_->SendUint32(message);
}

bool MonitorBase::InitSendPolicy() {
  bool user_notif = type_ == FORKSERVER_MONITOR_UNOTIFY;
  auto policy =
      policy_->GetPolicy(user_notif, executor_->enable_sandboxing_pre_execve_);
  absl::Status status = SendPolicy(std::move(policy));
  if (!status.ok()) {
    LOG(ERROR) << "Couldn't send policy: " << status;
    return false;
  }
  return true;
}

bool MonitorBase::InitSendCwd() {
  if (!comms_->SendString(executor_->cwd_)) {
    PLOG(ERROR) << "Couldn't send cwd";
    return false;
  }

  return true;
}

bool MonitorBase::InitApplyLimit(pid_t pid, int resource,
                                 const rlimit64& rlim) const {
  using RlimitResource = __rlimit_resource;

  rlimit64 curr_limit;
  if (prlimit64(pid, static_cast<RlimitResource>(resource), nullptr,
                &curr_limit) == -1) {
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

  if (prlimit64(pid, static_cast<RlimitResource>(resource), &rlim, nullptr) ==
      -1) {
    PLOG(ERROR) << "prlimit64(" << pid << ", " << util::GetRlimitName(resource)
                << ", " << rlim.rlim_cur << ")";
    return false;
  }

  return true;
}

bool MonitorBase::InitApplyLimits() {
  Limits* limits = executor_->limits();
  return InitApplyLimit(process_.main_pid, RLIMIT_AS, limits->rlimit_as()) &&
         InitApplyLimit(process_.main_pid, RLIMIT_CPU, limits->rlimit_cpu()) &&
         InitApplyLimit(process_.main_pid, RLIMIT_FSIZE,
                        limits->rlimit_fsize()) &&
         InitApplyLimit(process_.main_pid, RLIMIT_NOFILE,
                        limits->rlimit_nofile()) &&
         InitApplyLimit(process_.main_pid, RLIMIT_CORE, limits->rlimit_core());
}

bool MonitorBase::InitSendIPC() { return ipc_->SendFdsOverComms(); }

bool MonitorBase::WaitForSandboxReady() {
  uint32_t message;
  if (!comms_->RecvUint32(&message)) {
    LOG(ERROR) << "Couldn't receive 'Client::kClient2SandboxReady' message";
    return false;
  }
  if (message != Client::kClient2SandboxReady) {
    LOG(ERROR) << "Received " << message << " != Client::kClient2SandboxReady ("
               << Client::kClient2SandboxReady << ")";
    return false;
  }
  return true;
}

void MonitorBase::LogSyscallViolation(const Syscall& syscall) const {
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

void MonitorBase::LogSyscallViolationExplanation(const Syscall& syscall) const {
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

bool MonitorBase::StackTraceCollectionPossible() const {
  // Only get the stacktrace if we are not in the libunwind sandbox (avoid
  // recursion).
  if (executor_->libunwind_recursion_depth() <= 1) {
    return true;
  }
  LOG(ERROR) << "Cannot collect stack trace. Unwind pid "
             << executor_->libunwind_sbox_for_pid_ << ", namespace "
             << policy_->GetNamespaceOrNull();
  return false;
}

void MonitorBase::EnableNetworkProxyServer() {
  int fd = ipc_->ReceiveFd(NetworkProxyClient::kFDName);

  network_proxy_server_ = std::make_unique<NetworkProxyServer>(
      fd, &policy_->allowed_hosts_.value(),
      [this] { NotifyNetworkViolation(); });

  network_proxy_thread_ =
      sapi::Thread(network_proxy_server_.get(), &NetworkProxyServer::Run,
                   "NetworkProxyServer");
}

bool MonitorBase::ShouldCollectStackTrace(Result::StatusEnum status) const {
  if (!StackTraceCollectionPossible()) {
    return false;
  }
  switch (status) {
    case Result::EXTERNAL_KILL:
      return policy_->collect_stacktrace_on_kill_;
    case Result::TIMEOUT:
      return policy_->collect_stacktrace_on_timeout_;
    case Result::SIGNALED:
      return policy_->collect_stacktrace_on_signal_;
    case Result::VIOLATION:
      return policy_->collect_stacktrace_on_violation_;
    case Result::OK:
      return policy_->collect_stacktrace_on_exit_;
    default:
      return false;
  }
}

absl::StatusOr<std::vector<std::string>> MonitorBase::GetStackTrace(
    const Regs* regs) {
  return sandbox2::GetStackTrace(regs, policy_->GetNamespaceOrNull(),
                                 uses_custom_forkserver_,
                                 executor_->libunwind_recursion_depth() + 1);
}

absl::StatusOr<std::vector<std::string>> MonitorBase::GetAndLogStackTrace(
    const Regs* regs) {
  auto stack_trace = GetStackTrace(regs);
  if (!stack_trace.ok()) {
    return stack_trace.status();
  }

  LOG(INFO) << "Stack trace: [";
  for (const auto& frame : CompactStackTrace(*stack_trace)) {
    LOG(INFO) << "  " << frame;
  }
  LOG(INFO) << "]";

  return stack_trace;
}
}  // namespace sandbox2
