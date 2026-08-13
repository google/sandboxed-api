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

#ifndef SANDBOXED_API_NULL_BACKEND_H_
#define SANDBOXED_API_NULL_BACKEND_H_

#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "sandboxed_api/passthrough_rpcchannel.h"
#include "sandboxed_api/sandbox_config.h"

namespace sapi {

class SandboxBase;

class PassthroughBackend {
 public:
  using CallFunctionT = PassthroughRPCChannel::CallFunctionT;
  using SymbolFunctionT = PassthroughRPCChannel::SymbolFunctionT;

  PassthroughBackend(SandboxConfig config, CallFunctionT call_function,
                     SymbolFunctionT symbol_function)
      : rpc_channel_(std::make_unique<PassthroughRPCChannel>(
            std::move(call_function), std::move(symbol_function))) {}

  // Initializes a new sandboxing session.
  absl::Status Init() { return absl::OkStatus(); }

  // Returns whether the current sandboxing session is active.
  bool is_active() const { return true; }

  PassthroughRPCChannel* rpc_channel() const { return rpc_channel_.get(); }

  absl::Status SetWallTimeLimit(absl::Duration limit) const {
    // TODO(sroettger): This should enforce a wall time limit.
    return absl::OkStatus();
  }

  absl::Status AwaitExitStatus() { return absl::OkStatus(); }

  absl::StatusOr<int> GetPid() const {
    return absl::UnavailableError("In-process sandbox has no pid");
  }

  void Terminate(bool attempt_graceful_exit = true) {}

 private:
  std::unique_ptr<PassthroughRPCChannel> rpc_channel_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_NULL_BACKEND_H_
