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

#include "gtest/gtest.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace sandbox2 {
namespace {

TEST(LatencyStopWatchTest, IsAccurate) {
  constexpr absl::Duration kAccuracy = absl::Milliseconds(10);
  constexpr absl::Duration kSleepDuration = absl::Milliseconds(100);
  LatencyStopWatch latency_stop_watch;
  absl::SleepFor(kSleepDuration);
  EXPECT_GT(latency_stop_watch.LapTime(), kSleepDuration - kAccuracy / 2);
  EXPECT_LT(latency_stop_watch.LapTime(), kSleepDuration + kAccuracy / 2);
}

}  // namespace
}  // namespace sandbox2
