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
#ifndef SANDBOXED_API_SANDBOX2_SETUP_LATENCY_BREAKDOWN_H_
#define SANDBOXED_API_SANDBOX2_SETUP_LATENCY_BREAKDOWN_H_

#include <array>
#include <string>

#include "absl/time/time.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/util/raw_logging.h"

namespace sandbox2 {

class SetupLatencyBreakdown {
 public:
  enum SetupStep {
    // --- Common Setup Steps ---
    kSharedNamespacesCreation,
    kSetupCommsCreation,
    kSetupProcessFork,

    // --- Standard Namespace Mode Steps ---
    kInitFork,
    kTillNamespacesUnshare,
    kNamespacesUnshare,
    kNamespacesInitialization,
    kNsInitChrootToRealRoot,
    kNsInitProcMount,
    kNsInitNetnsInitialization,
    kNsInitPrepareChroot,
    kNsInitChrootBack,
    kNsInitUnmountRealRoot,
    kNsInitNestedUserNamespace,
    kInitLaunch,

    // --- Shared PID / Landlock Isolation Mode Steps ---
    kSharedPidInitFork,
    kSharedPidTillNamespacesUnshare,
    kSharedPidNamespacesUnshare,
    kSharedPidNetnsInitialization,
    kSharedPidNestedUserNamespace,
    kSharedPidLandlockEnforcement,

    // --- Final Common Step ---
    kTillAlmostDone,
    kNumSetupSteps,
  };

  static std::string SetupStepToString(SetupStep step);

  SetupLatencyBreakdown() = default;
  SetupLatencyBreakdown(const SetupLatencyBreakdown&) = default;
  SetupLatencyBreakdown& operator=(const SetupLatencyBreakdown&) = default;

  void SetLatency(SetupStep step, absl::Duration duration) {
    SAPI_RAW_CHECK(step < kNumSetupSteps, "Invalid setup step");
    latencies_[step] = duration;
  }
  absl::Duration GetLatency(SetupStep step) const { return latencies_[step]; }

  bool Send(Comms& comms);
  bool Receive(Comms& comms);

 private:
  std::array<absl::Duration, SetupStep::kNumSetupSteps> latencies_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_SETUP_LATENCY_BREAKDOWN_H_
