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

// The sandbox2::Sandbox object is the central object of the Sandbox2.
// It handles sandboxed jobs.

#ifndef SANDBOXED_API_SANDBOX2_SANDBOX2_H_
#define SANDBOXED_API_SANDBOX2_SANDBOX2_H_

#include <ctime>
#include <memory>
#include <thread>  // NOLINT(build/c++11)
#include <utility>

#include "absl/base/macros.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/ipc.h"
#include "sandboxed_api/sandbox2/monitor.h"
#include "sandboxed_api/sandbox2/notify.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/result.h"

namespace sandbox2 {

class Sandbox2 final {
 public:
  Sandbox2(std::unique_ptr<Executor> executor, std::unique_ptr<Policy> policy)
      : Sandbox2(std::move(executor), std::move(policy), /*notify=*/nullptr) {}

  Sandbox2(std::unique_ptr<Executor> executor, std::unique_ptr<Policy> policy,
           std::unique_ptr<Notify> notify)
      : executor_(std::move(executor)),
        policy_(std::move(policy)),
        notify_(std::move(notify)) {
    CHECK(executor_ != nullptr);
    CHECK(policy_ != nullptr);
    if (notify_ == nullptr) {
      notify_ = std::make_unique<Notify>();
    }
  }

  ~Sandbox2();

  Sandbox2(const Sandbox2&) = delete;
  Sandbox2& operator=(const Sandbox2&) = delete;

  // Runs the sandbox, blocking until there is a result.
  ABSL_MUST_USE_RESULT Result Run() {
    RunAsync();
    return AwaitResult();
  }

  // Runs asynchronously. The return value indicates whether the sandboxee
  // set-up process succeeded
  // Even if set-up fails AwaitResult can still used to get a more specific
  // failure reason.
  bool RunAsync();
  // Waits for sandbox execution to finish and returns the execution result.
  ABSL_MUST_USE_RESULT Result AwaitResult();

  // Waits for sandbox execution to finish within the timeout.
  // Returns execution result or a DeadlineExceededError if the sandboxee does
  // not finish in time.
  absl::StatusOr<Result> AwaitResultWithTimeout(absl::Duration timeout);

  // Requests termination of the sandboxee.
  // Sandbox should still waited with AwaitResult(), as it may finish for other
  // reason before the request is handled.
  void Kill();

  // Dumps the main sandboxed process's stack trace to log.
  void DumpStackTrace();

  // Returns whether sandboxing task has ended.
  bool IsTerminated() const;

  // Sets a wall time limit on a running sandboxee, 0 to disarm.
  // Limit is a timeout duration (e.g. 10 secs) not a deadline (e.g. 12:00).
  // This can be useful in a persistent sandbox scenario, to impose a deadline
  // for responses after each request and reset the deadline in between.
  // Sandboxed API can be used to implement persistent sandboxes.
  ABSL_DEPRECATED("Use set_walltime_limit() instead")
  void SetWallTimeLimit(time_t limit) const {
    this->set_walltime_limit(absl::Seconds(limit));
  }

  // Sets a wall time limit on a running sandboxee, absl::ZeroDuration() to
  // disarm. This can be useful in a persistent sandbox scenario, to impose a
  // deadline for responses after each request and reset the deadline in
  // between. Sandboxed API can be used to implement persistent sandboxes.
  void set_walltime_limit(absl::Duration limit) const;

  // Returns the process id inside the executor.
  pid_t pid() const { return monitor_ != nullptr ? monitor_->pid_ : -1; }

  // Gets the comms inside the executor.
  Comms* comms() {
    return executor_ != nullptr ? executor_->ipc()->comms() : nullptr;
  }

 private:
  // Launches the Monitor.
  void Launch();
  // Notifies monitor about a state change
  void NotifyMonitor();

  // Executor set by user - owned by Sandbox2.
  std::unique_ptr<Executor> executor_;

  // Seccomp policy set by the user - owned by Sandbox2.
  std::unique_ptr<Policy> policy_;

  // Notify object - owned by Sandbox2.
  std::unique_ptr<Notify> notify_;

  // Monitor object - owned by Sandbox2.
  std::unique_ptr<Monitor> monitor_;

  // Monitor thread object - owned by Sandbox2.
  std::unique_ptr<std::thread> monitor_thread_;

  // Synchronizes monitor thread deletion and notifying the monitor.
  absl::Mutex monitor_notify_mutex_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_SANDBOX2_H_
