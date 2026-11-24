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

#ifndef SANDBOXED_API_LWBOX_RUNTIME_SANDBOX_CONFIG_H_
#define SANDBOXED_API_LWBOX_RUNTIME_SANDBOX_CONFIG_H_

#include <cstddef>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "sandboxed_api/embed_toc.h"
#include "sandboxed_api/sandbox.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox_config.h"

namespace sapi::lwbox {

// Returns the default PolicyBuilder for lightweight sandboxed libraries.
sandbox2::PolicyBuilder DefaultPolicyBuilder();

// Builds a SandboxConfig for a merged lightweight sandbox from its extracted
// seccomp `policy` and embedded binary `embed_toc`.
::sapi::SandboxConfig CreateMergedSandboxConfig(
    std::unique_ptr<sandbox2::Policy> policy,
    const ::sapi::EmbedToc* embed_toc);

// Sandbox shared across all merged lightweight sandboxed libraries in a binary.
//
// Generated code only uses the `SandboxBase` interface, so this class forwards
// to a sandbox with whichever backend the binary uses. Initializes `sandbox` on
// construction and crashes if that fails.
class MergedSandbox final : public ::sapi::SandboxBase {
 public:
  explicit MergedSandbox(std::unique_ptr<::sapi::SandboxBase> sandbox);

  absl::Status Init() override { return sandbox_->Init(); }
  bool is_active() const override { return sandbox_->is_active(); }
  void Terminate(bool attempt_graceful_exit = true) override {
    sandbox_->Terminate(attempt_graceful_exit);
  }
  // NOLINTNEXTLINE(misc-include-cleaner)
  ::sapi::RPCChannel* rpc_channel() const override {
    return sandbox_->rpc_channel();
  }
  absl::Status AwaitExitStatus() override {
    return sandbox_->AwaitExitStatus();
  }
  absl::Status SetWallTimeLimit(absl::Duration limit) const override {
    return sandbox_->SetWallTimeLimit(limit);
  }
  absl::StatusOr<int> GetPid() const override { return sandbox_->GetPid(); }

  static void Check(const absl::Status& status);

  static size_t CheckedMultiply(size_t x, size_t y);

 private:
  std::unique_ptr<::sapi::SandboxBase> sandbox_;
};

// Returns the process-wide shared MergedSandbox instance, creating it on the
// first call. The Sandbox2 backend configures it with `create_config`.
//
// The merged sandbox build rule links the definition for the backend the
// binary uses, e.g. `:sandbox2_merged_sandbox_host`.
MergedSandbox& SharedSandboxInstance(::sapi::SandboxConfig (*create_config)());

}  // namespace sapi::lwbox

#endif  // SANDBOXED_API_LWBOX_RUNTIME_SANDBOX_CONFIG_H_
