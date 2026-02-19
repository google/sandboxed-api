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

#ifndef SANDBOXED_API_SANDBOX2_BACKEND_H_
#define SANDBOXED_API_SANDBOX2_BACKEND_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/notify.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox_config.h"

namespace sapi {

class SandboxBase;

class Sandbox2Backend {
 public:
  explicit Sandbox2Backend(SandboxBase* sandbox, SandboxConfig config);

  virtual ~Sandbox2Backend();

  // Initializes a new sandboxing session.
  absl::Status Init();

  // Returns whether the current sandboxing session is active.
  bool is_active() const;

  // Terminates the current sandboxing session (if it exists).
  void Terminate(bool attempt_graceful_exit = true);

  sandbox2::Comms* comms() const { return comms_; }

  RPCChannel* rpc_channel() const { return rpc_channel_.get(); }

  // Waits until the sandbox terminated and returns the result.
  const sandbox2::Result& AwaitResult();
  const sandbox2::Result& result() const { return result_; }

  absl::Status ResultStatus() { return AwaitResult().ToStatus(); }

  absl::Status SetWallTimeLimit(absl::Duration limit) const;

  int pid() const { return pid_; }

  absl::StatusOr<int> GetPid() const;

 private:
  friend class SandboxBase;

  // Provides a custom notifier for sandboxee events. May return nullptr.
  virtual std::unique_ptr<sandbox2::Notify> CreateNotifier();

  void ApplySandbox2Config(sandbox2::Executor* executor) const;
  void MapFileDescriptors(sandbox2::Executor* executor) const;

  std::vector<std::string> EnvironmentVariables() const {
    return config_.environment_variables.value_or(
        SandboxConfig::DefaultEnvironmentVariables());
  }

  const ForkClientContext& fork_client_context() const {
    return config_.sandbox2.fork_client_context.value();
  }
  ForkClientContext::SharedState& fork_client_shared() const {
    return *fork_client_context().shared_;
  }

  SandboxConfig config_;

  // TODO(sroettger): Remove this pointer after migrating all users of
  // CreateNotifier().
  SandboxBase* sandbox_base_;

  // The main sandbox2::Sandbox2 object.
  std::unique_ptr<sandbox2::Sandbox2> s2_;
  // Marks whether Sandbox2 result was already fetched.
  // We cannot just delete s2_ as Terminate might be called from another thread
  // and comms object can be still in use then.
  bool s2_awaited_ = false;

  // Result of the most recent sandbox execution
  sandbox2::Result result_;

  // Comms with the sandboxee.
  sandbox2::Comms* comms_ = nullptr;
  // RPCChannel object.
  std::unique_ptr<RPCChannel> rpc_channel_;
  // The main pid of the sandboxee.
  pid_t pid_ = 0;
};

}  // namespace sapi

#endif  // SANDBOXED_API_SANDBOX2_BACKEND_H_
