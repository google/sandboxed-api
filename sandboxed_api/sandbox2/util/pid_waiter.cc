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

#include "sandboxed_api/sandbox2/util/pid_waiter.h"

#include <sys/wait.h>

#include <cerrno>
#include <memory>

#include "absl/cleanup/cleanup.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "sandboxed_api/sandbox2/util/deadline_manager.h"

namespace sandbox2 {

namespace {

class OsWaitPid : public PidWaiter::WaitPidInterface {
 public:
  int WaitPid(pid_t pid, int* status, int flags,
              struct rusage* rusage) override {
    return wait4(pid, status, flags, rusage);
  }
};

}  // namespace

PidWaiter::PidWaiter(pid_t priority_pid)
    : PidWaiter(priority_pid, std::make_unique<OsWaitPid>()) {}

int PidWaiter::Wait(int* status, struct rusage* rusage) {
  RefillStatuses();

  if (statuses_.empty()) {
    if (last_errno_ == 0) return 0;
    errno = last_errno_;
    last_errno_ = 0;
    return -1;
  }

  const auto& entry = statuses_.front();
  pid_t pid = entry.pid;
  *status = entry.status;
  if (rusage) {
    *rusage = entry.rusage;
  }
  statuses_.pop_front();
  return pid;
}

bool PidWaiter::CheckStatus(pid_t pid, bool blocking) {
  int status;
  struct rusage rusage;
  int flags = __WNOTHREAD | __WALL | WUNTRACED;
  if (!blocking) {
    // It should be a non-blocking operation (hence WNOHANG), so this function
    // returns quickly if there are no events to be processed.
    flags |= WNOHANG;
  }
  pid_t ret = wait_pid_iface_->WaitPid(pid, &status, flags, &rusage);
  if (ret < 0) {
    last_errno_ = errno;
    return true;
  }
  if (ret == 0) {
    return false;
  }
  statuses_.push_back({.pid = ret, .status = status, .rusage = rusage});
  return true;
}

void PidWaiter::RefillStatuses() {
  constexpr int kMaxIterations = 1000;
  constexpr int kPriorityCheckPeriod = 100;
  absl::Cleanup notify = [this] {
    absl::MutexLock lock(notify_mutex_);
    notified_ = false;
  };
  if (!statuses_.empty()) {
    return;
  }
  for (int i = 0; last_errno_ == 0 && i < kMaxIterations; ++i) {
    bool should_check_priority =
        priority_pid_ != -1 && (i % kPriorityCheckPeriod) == 0;
    if (should_check_priority && CheckStatus(priority_pid_)) {
      return;
    }
    if (!CheckStatus(-1)) {
      break;
    }
  }
  if (statuses_.empty()) {
    DeadlineRegistration* deadline_registration = nullptr;
    {
      absl::MutexLock lock(notify_mutex_);
      if (deadline_ == absl::InfinitePast() || notified_) {
        return;
      }
      if (!deadline_registration_.has_value()) {
        deadline_registration_.emplace(DeadlineManager::instance());
      }
      deadline_registration_->SetDeadline(deadline_);
      // DeadlineRegistration is only constructed once, so it's safe to use it
      // outside of the lock.
      deadline_registration = &*deadline_registration_;
    }
    deadline_registration->ExecuteBlockingSyscall(
        [&] { CheckStatus(-1, /*blocking=*/true); });
  }
}

}  // namespace sandbox2
