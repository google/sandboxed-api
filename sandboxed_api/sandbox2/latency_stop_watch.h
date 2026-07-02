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
#ifndef SANDBOXED_API_SANDBOX2_LATENCY_STOP_WATCH_H_
#define SANDBOXED_API_SANDBOX2_LATENCY_STOP_WATCH_H_

#include <cstdint>

#include "absl/time/time.h"

namespace sandbox2 {

// Used to measure latency as time between certain points in code execution.
class LatencyStopWatch {
 public:
  LatencyStopWatch();

  LatencyStopWatch(const LatencyStopWatch&) = default;
  LatencyStopWatch& operator=(const LatencyStopWatch&) = default;

  // Returns the time between the last lap (or the ctor) and the current moment.
  absl::Duration LapTime();

 private:
  int64_t current_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_LATENCY_STOP_WATCH_H_
