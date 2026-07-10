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

#ifndef SANDBOXED_API_SANDBOX2_FORKEDPROCESS_H_
#define SANDBOXED_API_SANDBOX2_FORKEDPROCESS_H_

#include <string>
#include <utility>
#include <vector>

#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/sandbox2/latency_stop_watch.h"
#include "sandboxed_api/sandbox2/setup_latency_breakdown.h"
#include "sandboxed_api/util/fileops.h"

namespace sandbox2 {

class ForkedProcess {
 public:
  ForkedProcess(ForkRequest request, Comms setup_comms,
                SetupLatencyBreakdown latency_breakdown)
      : request_(std::move(request)),
        setup_comms_(std::move(setup_comms)),
        latency_breakdown_(std::move(latency_breakdown)) {}
  // Returns the comms channel for the newly setup sandboxee.
  Comms Setup();

 private:
  void ReceiveFDs(bool will_exec);
  void SanitizeEnvironment();
  sapi::file_util::fileops::FDCloser CreateAndSendStatusPipe();
  void LaunchInit();
  void PrepareExecveArgs();
  void JoinInitialUserNamespace();
  void JoinNamespaces();
  void JoinNetworkNamespace();
  void SetupNamespaces();
  void SetupLandlockNamespaces();
  void MoveToPredefiedFDs();
  void LaunchSandboxee();

  ForkRequest request_;
  Comms setup_comms_;
  SetupLatencyBreakdown latency_breakdown_;
  LatencyStopWatch latency_stop_watch_;
  std::vector<std::string> args_;
  std::vector<std::string> envp_;
  sapi::file_util::fileops::FDCloser comms_fd_;
  sapi::file_util::fileops::FDCloser execve_fd_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_FORKEDPROCESS_H_
