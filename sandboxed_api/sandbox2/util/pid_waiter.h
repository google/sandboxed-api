// Copyright 2024 Google LLC
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

#ifndef SANDBOXED_API_SANDBOX2_UTIL_PID_WAITER_H_
#define SANDBOXED_API_SANDBOX2_UTIL_PID_WAITER_H_

#include <sys/wait.h>

#include <deque>
#include <memory>
#include <optional>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "sandboxed_api/sandbox2/util/deadline_manager.h"

namespace sandbox2 {

// Since waitpid() is biased towards newer threads, we run the risk of
// starving older threads if the newer ones raise a lot of events. To avoid
// it, we use this class to gather all the waiting threads and then return
// them one at a time on each call to Wait(). In this way, everyone gets their
// chance.
class PidWaiter {
 public:
  // Interface for waitpid() to allow mocking it in tests.
  class WaitPidInterface {
   public:
    virtual int WaitPid(pid_t pid, int* status, int flags) = 0;
    virtual ~WaitPidInterface() = default;
  };

  // Constructs a PidWaiter where the given priority_pid is checked first.
  explicit PidWaiter(pid_t priority_pid = -1);
  PidWaiter(pid_t priority_pid,
            std::unique_ptr<WaitPidInterface> wait_pid_iface)
      : priority_pid_(priority_pid),
        wait_pid_iface_(std::move(wait_pid_iface)) {};

  // Returns the PID of a thread that needs attention, populating 'status'
  // with the status returned by the waitpid() call. It returns 0 if no
  // threads require attention at the moment, or -1 if there was an error, in
  // which case the error value can be found in 'errno'.
  int Wait(int* status);

  void SetPriorityPid(pid_t pid) { priority_pid_ = pid; }

  // Sets the deadline for the next Wait() call.
  void SetDeadline(absl::Time deadline) {
    absl::MutexLock lock(&notify_mutex_);
    deadline_ = deadline;
  }

  // Breaks out of the current Wait operation, if there is one running
  // concurrently. Otherwise, makes the next Wait non-blocking.
  // Can be called concurrently with Wait and SetDeadline.
  void Notify() {
    absl::MutexLock lock(&notify_mutex_);
    if (deadline_registration_.has_value()) {
      deadline_registration_->SetDeadline(absl::InfinitePast());
    }
    notified_ = true;
  }

 private:
  bool CheckStatus(pid_t pid, bool blocking = false);
  void RefillStatuses();

  pid_t priority_pid_;
  std::deque<std::pair<pid_t, int>> statuses_;
  std::unique_ptr<WaitPidInterface> wait_pid_iface_;
  int last_errno_ = 0;
  absl::Mutex notify_mutex_;
  absl::Time deadline_ ABSL_GUARDED_BY(notify_mutex_) = absl::InfinitePast();
  std::optional<DeadlineRegistration> deadline_registration_
      ABSL_GUARDED_BY(notify_mutex_);
  bool notified_ ABSL_GUARDED_BY(notify_mutex_) = false;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_UTIL_PID_WAITER_H_
