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
#include "sandboxed_api/sandbox2/setup_latency_breakdown.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/time/time.h"
#include "sandboxed_api/sandbox2/comms.h"

namespace sandbox2 {

std::string SetupLatencyBreakdown::SetupStepToString(SetupStep step) {
  switch (step) {
    // --- Common Setup Steps ---
    case kSharedNamespacesCreation:
      return "Shared namespaces creation";
    case kSetupCommsCreation:
      return "Setup comms creation";
    case kSetupProcessFork:
      return "Setup process fork";
    case kInitFork:
      return "Init process fork";
    case kTillNamespacesUnshare:
      return "Till namespaces unshare";
    case kNamespacesUnshare:
      return "Namespaces unshare";
    case kNamespacesInitialization:
      return "Namespaces initialization";
    case kNsInitChrootToRealRoot:
      return "Chroot to realroot";
    case kNsInitProcMount:
      return "Procfs mount";
    case kNsInitNetnsInitialization:
      return "Netns init";
    case kNsInitPrepareChroot:
      return "Prepare chroot";
    case kNsInitChrootBack:
      return "Chroot to sandboxee";
    case kNsInitUnmountRealRoot:
      return "Unmount realroot";
    case kNsInitNestedUserNamespace:
      return "Nested userns init";
    case kInitLaunch:
      return "Init process launch";
    case kSharedPidInitFork:
      return "Shared PID init process fork";

    // --- Landlock Isolation Mode Steps ---
    case kLandlockEnforcement:
      return "Landlock enforcement";

    // --- Final Common Step ---
    case kTillAlmostDone:
      return "Until almost done";
    default:
      return "Unknown step";
  }
}

bool SetupLatencyBreakdown::Send(Comms& comms) {
  std::array<int64_t, kNumSetupSteps> latencies_micros;
  for (int i = 0; i < kNumSetupSteps; ++i) {
    latencies_micros[i] = absl::ToInt64Microseconds(latencies_[i]);
  }
  return comms.SendTLV(Comms::kTagLatencyBreakdown,
                       sizeof(int64_t) * latencies_micros.size(),
                       latencies_micros.data());
}

bool SetupLatencyBreakdown::Receive(Comms& comms) {
  std::array<int64_t, kNumSetupSteps> latencies_micros;
  uint32_t tag;
  size_t length;
  if (!comms.RecvTLV(&tag, &length, latencies_micros.data(),
                     sizeof(int64_t) * latencies_micros.size(),
                     Comms::kTagLatencyBreakdown) ||
      length != sizeof(int64_t) * kNumSetupSteps) {
    return false;
  }
  for (int i = 0; i < kNumSetupSteps; ++i) {
    latencies_[i] = absl::Microseconds(latencies_micros[i]);
  }
  return true;
}

}  // namespace sandbox2
