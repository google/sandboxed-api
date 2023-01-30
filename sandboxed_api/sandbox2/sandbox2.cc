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
#include "absl/time/time.h"
#include "sandboxed_api/sandbox2/monitor_ptrace.h"
#include "sandboxed_api/sandbox2/result.h"

namespace sandbox2 {

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
  monitor_ = std::make_unique<PtraceMonitor>(executor_.get(), policy_.get(),
                                             notify_.get());
  monitor_->Launch();
}

}  // namespace sandbox2
