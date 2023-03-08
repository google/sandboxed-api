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

// Implementation file for the sandbox2::Sandbox class.

#include "sandboxed_api/sandbox2/sandbox2.h"

#include <csignal>
#include <memory>
#include <string>
#include <vector>

#include "absl/base/call_once.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "sandboxed_api/sandbox2/monitor_base.h"
#include "sandboxed_api/sandbox2/monitor_ptrace.h"
#include "sandboxed_api/sandbox2/monitor_unotify.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/stack_trace.h"

namespace sandbox2 {

namespace {

class Sandbox2Peer : public internal::SandboxPeer {
 public:
  static std::unique_ptr<SandboxPeer> Spawn(std::unique_ptr<Executor> executor,
                                            std::unique_ptr<Policy> policy) {
    return std::make_unique<Sandbox2Peer>(std::move(executor),
                                          std::move(policy));
  }

  Sandbox2Peer(std::unique_ptr<Executor> executor,
               std::unique_ptr<Policy> policy)
      : sandbox_(std::move(executor), std::move(policy)) {
    sandbox_.RunAsync();
  }

  Comms* comms() override { return sandbox_.comms(); }
  void Kill() override { sandbox_.Kill(); }
  Result AwaitResult() override { return sandbox_.AwaitResult(); }

 private:
  Sandbox2 sandbox_;
};

}  // namespace

absl::StatusOr<Result> Sandbox2::AwaitResultWithTimeout(
    absl::Duration timeout) {
  CHECK(monitor_ != nullptr) << "Sandbox was not launched yet";
  return monitor_->AwaitResultWithTimeout(timeout);
}

Result Sandbox2::AwaitResult() {
  return AwaitResultWithTimeout(absl::InfiniteDuration()).value();
}

bool Sandbox2::RunAsync() {
  Launch();

  // If the sandboxee setup failed we return 'false' here.
  if (monitor_->IsDone() &&
      monitor_->result().final_status() == Result::SETUP_ERROR) {
    return false;
  }
  return true;
}

void Sandbox2::Kill() {
  CHECK(monitor_ != nullptr) << "Sandbox was not launched yet";
  monitor_->Kill();
}

void Sandbox2::DumpStackTrace() {
  CHECK(monitor_ != nullptr) << "Sandbox was not launched yet";
  monitor_->DumpStackTrace();
}

bool Sandbox2::IsTerminated() const {
  CHECK(monitor_ != nullptr) << "Sandbox was not launched yet";
  return monitor_->IsDone();
}

void Sandbox2::set_walltime_limit(absl::Duration limit) const {
  CHECK(monitor_ != nullptr) << "Sandbox was not launched yet";
  monitor_->SetWallTimeLimit(limit);
}

void Sandbox2::Launch() {
  static absl::once_flag init_sandbox_peer_flag;
  absl::call_once(init_sandbox_peer_flag, []() {
    internal::SandboxPeer::spawn_fn_ = Sandbox2Peer::Spawn;
  });

  monitor_ = CreateMonitor();
  monitor_->Launch();
}

absl::Status Sandbox2::EnableUnotifyMonitor() {
  if (notify_) {
    return absl::FailedPreconditionError(
        "sandbox2::Notify is not compatible with unotify monitor");
  }
  if (policy_->GetNamespace() == nullptr) {
    return absl::FailedPreconditionError(
        "Unotify monitor can only be used together with namespaces");
  }
  if (policy_->collect_stacktrace_on_signal_) {
    return absl::FailedPreconditionError(
        "Unotify monitor cannot collect stack traces on signal");
  }

  if (policy_->collect_stacktrace_on_exit_) {
    return absl::FailedPreconditionError(
        "Unotify monitor cannot collect stack traces on normal exit");
  }
  use_unotify_monitor_ = true;
  return absl::OkStatus();
}

std::unique_ptr<MonitorBase> Sandbox2::CreateMonitor() {
  if (!notify_) {
    notify_ = std::make_unique<Notify>();
  }
  if (use_unotify_monitor_) {
    return std::make_unique<UnotifyMonitor>(executor_.get(), policy_.get(),
                                            notify_.get());
  }
  return std::make_unique<PtraceMonitor>(executor_.get(), policy_.get(),
                                         notify_.get());
}

}  // namespace sandbox2
