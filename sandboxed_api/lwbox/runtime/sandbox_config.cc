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

#include "sandboxed_api/lwbox/runtime/sandbox_config.h"

#include "sandboxed_api/sandbox2/allowlists/seccomp_speculation.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox_config.h"

namespace sapi::lwbox {

sandbox2::PolicyBuilder DefaultPolicyBuilder() {
  sandbox2::PolicyBuilder builder =
      sapi::Sandbox2Config::DefaultPolicyBuilder();
  builder.AllowMultithreading();
  builder.Allow(sandbox2::SeccompSpeculation());
  return builder;
}

}  // namespace sapi::lwbox
