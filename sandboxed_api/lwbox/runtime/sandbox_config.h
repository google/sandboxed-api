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
#include "sandboxed_api/embed_toc.h"
#include "sandboxed_api/sandbox.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2_backend.h"
#include "sandboxed_api/sandbox_config.h"

namespace sapi::lwbox {

// Returns the default PolicyBuilder for lightweight sandboxed libraries.
sandbox2::PolicyBuilder DefaultPolicyBuilder();

// Builds a SandboxConfig for a merged lightweight sandbox from its extracted
// seccomp `policy` and embedded binary `embed_toc`.
::sapi::SandboxConfig CreateMergedSandboxConfig(
    std::unique_ptr<sandbox2::Policy> policy,
    const ::sapi::EmbedToc* embed_toc);

// Concrete sandbox class shared across all merged lightweight sandboxed
// libraries in a binary.
class MergedSandbox : public ::sapi::Sandbox<::sapi::Sandbox2Backend> {
 public:
  explicit MergedSandbox(::sapi::SandboxConfig config);

  static void Check(const absl::Status& status);

  static size_t CheckedMultiply(size_t x, size_t y);
};

// Returns the process-wide shared MergedSandbox instance, initializing it via
// `create_config` on first call.
MergedSandbox& SharedSandboxInstance(::sapi::SandboxConfig (*create_config)());

}  // namespace sapi::lwbox

#endif  // SANDBOXED_API_LWBOX_RUNTIME_SANDBOX_CONFIG_H_
