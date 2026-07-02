// Copyright 2026 Google LLC
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
#include "sandboxed_api/sandbox2/latency_stop_watch.h"

#include <chrono>
#include <cstdint>

#include "absl/time/time.h"

namespace sandbox2 {
namespace {

int64_t Now() {
  return std::chrono::steady_clock::now().time_since_epoch().count();
}

absl::Duration DurationBetween(int64_t start, int64_t end) {
  using period = std::chrono::steady_clock::duration::period;
  return absl::Microseconds((end - start) * 1000000 * period::num /
                            period::den);
}
}  // namespace

LatencyStopWatch::LatencyStopWatch() { current_ = Now(); }

absl::Duration LatencyStopWatch::LapTime() {
  int64_t last = current_;
  current_ = Now();
  return DurationBetween(last, current_);
}

}  // namespace sandbox2
