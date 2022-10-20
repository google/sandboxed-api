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

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "sandboxed_api/sandbox2/monitor.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/util/raw_logging.h"

namespace sandbox2 {

Sandbox2::~Sandbox2() {
  if (monitor_thread_ && monitor_thread_->joinable()) {
    monitor_thread_->join();
  }
}

absl::StatusOr<Result> Sandbox2::AwaitResultWithTimeout(
    absl::Duration timeout) {
  CHECK(monitor_ != nullptr) << "Sandbox was not launched yet";
  CHECK(monitor_thread_ != nullptr) << "Sandbox was already waited on";

  auto done =
      monitor_->done_notification_.WaitForNotificationWithTimeout(timeout);
  if (!done) {
    return absl::DeadlineExceededError("Sandbox did not finish within timeout");
  }
  {
    absl::MutexLock lock(&monitor_notify_mutex_);
    monitor_thread_->join();

    CHECK(IsTerminated()) << "Monitor did not terminate";

    // Reset the Monitor Thread object to its initial state, as to mark that
    // this object cannot be used anymore to control behavior of the sandboxee
    // (e.g. via signals).
    monitor_thread_.reset();
  }

  VLOG(1) << "Final execution status: " << monitor_->result_.ToString();
  CHECK(monitor_->result_.final_status() != Result::UNSET);
  return std::move(monitor_->result_);
}

Result Sandbox2::AwaitResult() {
  return AwaitResultWithTimeout(absl::InfiniteDuration()).value();
}

bool Sandbox2::RunAsync() {
  Launch();

  // If the sandboxee setup failed we return 'false' here.
  if (monitor_->IsDone() &&
      monitor_->result_.final_status() == Result::SETUP_ERROR) {
    return false;
  }
  return true;
}

void Sandbox2::NotifyMonitor() {
  absl::ReaderMutexLock lock(&monitor_notify_mutex_);
  if (monitor_thread_ != nullptr) {
    pthread_kill(monitor_thread_->native_handle(), SIGCHLD);
  }
}

void Sandbox2::Kill() {
  CHECK(monitor_ != nullptr) << "Sandbox was not launched yet";

  monitor_->external_kill_request_flag_.clear(std::memory_order_relaxed);
  NotifyMonitor();
}

void Sandbox2::DumpStackTrace() {
  CHECK(monitor_ != nullptr) << "Sandbox was not launched yet";

  monitor_->dump_stack_request_flag_.clear(std::memory_order_relaxed);
  NotifyMonitor();
}

bool Sandbox2::IsTerminated() const {
  CHECK(monitor_ != nullptr) << "Sandbox was not launched yet";

  return monitor_->IsDone();
}

void Sandbox2::set_walltime_limit(absl::Duration limit) const {
  if (limit == absl::ZeroDuration()) {
    VLOG(1) << "Disarming walltime timer to ";
    monitor_->deadline_millis_.store(0, std::memory_order_relaxed);
  } else {
    VLOG(1) << "Will set the walltime timer to " << limit;
    absl::Time deadline = absl::Now() + limit;
    monitor_->deadline_millis_.store(absl::ToUnixMillis(deadline),
                                     std::memory_order_relaxed);
  }
}

void Sandbox2::Launch() {
  monitor_ =
      std::make_unique<Monitor>(executor_.get(), policy_.get(), notify_.get());
  monitor_thread_ =
      std::make_unique<std::thread>(&Monitor::Run, monitor_.get());

  // Wait for the Monitor to set-up the sandboxee correctly (or fail while
  // doing that). From here on, it is safe to use the IPC object for
  // non-sandbox-related data exchange.
  monitor_->setup_notification_.WaitForNotification();
}

}  // namespace sandbox2
