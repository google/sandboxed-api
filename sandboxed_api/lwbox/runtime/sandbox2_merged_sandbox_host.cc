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

// Host side of the merged lightweight sandbox with Sandbox2.

#include <memory>
#include <utility>

#include "absl/base/no_destructor.h"
#include "sandboxed_api/lwbox/runtime/sandbox_config.h"
#include "sandboxed_api/sandbox.h"
#include "sandboxed_api/sandbox2_backend.h"
#include "sandboxed_api/sandbox_config.h"

namespace sapi::lwbox {

MergedSandbox& SharedSandboxInstance(::sapi::SandboxConfig (*create_config)()) {
  static absl::NoDestructor<MergedSandbox> instance([create_config] {
    ::sapi::SandboxConfig config = create_config();
    if (config.name.empty() || config.name == "unknown") {
      config.set_name("SapiMergedSandbox");
    }
    return std::make_unique<::sapi::Sandbox<::sapi::Sandbox2Backend>>(
        std::move(config));
  }());
  return *instance;
}

}  // namespace sapi::lwbox
