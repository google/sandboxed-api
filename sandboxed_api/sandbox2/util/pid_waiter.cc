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

#include <cerrno>
#include <memory>

namespace sandbox2 {

namespace {

class OsWaitPid : public PidWaiter::WaitPidInterface {
 public:
  int WaitPid(pid_t pid, int* status, int flags) override {
    return waitpid(pid, status, flags);
  }
};

}  // namespace

PidWaiter::PidWaiter(pid_t priority_pid)
    : PidWaiter(priority_pid, std::make_unique<OsWaitPid>()) {}

int PidWaiter::Wait(int* status) {
  RefillStatuses();

  if (statuses_.empty()) {
    if (last_errno_ == 0) return 0;
    errno = last_errno_;
    last_errno_ = 0;
    return -1;
  }

  const auto& entry = statuses_.front();
  pid_t pid = entry.first;
  *status = entry.second;
  statuses_.pop_front();
  return pid;
}

bool PidWaiter::CheckStatus(pid_t pid, bool blocking) {
  int status;
  int flags = __WNOTHREAD | __WALL | WUNTRACED;
  if (!blocking) {
    // It should be a non-blocking operation (hence WNOHANG), so this function
    // returns quickly if there are no events to be processed.
    flags |= WNOHANG;
  }
  pid_t ret = wait_pid_iface_->WaitPid(pid, &status, flags);
  if (ret < 0) {
    last_errno_ = errno;
    return true;
  }
  if (ret == 0) {
    return false;
  }
  statuses_.emplace_back(ret, status);
  return true;
}

void PidWaiter::RefillStatuses() {
  constexpr int kMaxIterations = 1000;
  constexpr int kPriorityCheckPeriod = 100;
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
  if (statuses_.empty() && deadline_registration_.has_value()) {
    deadline_registration_->ExecuteBlockingSyscall(
        [&] { CheckStatus(-1, /*blocking=*/true); });
  }
}

}  // namespace sandbox2
