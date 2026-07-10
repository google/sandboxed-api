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

#ifndef SANDBOXED_API_SANDBOX2_ALLOWLISTS_ENABLE_LANDLOCK_H_
#define SANDBOXED_API_SANDBOX2_ALLOWLISTS_ENABLE_LANDLOCK_H_

namespace sandbox2 {

// Token required to enable Landlock mode in Sandbox2.
//
// Note: Landlock support is experimental and subject to change.
// Visibility of this token is restricted via build rules to retain control
// over rollout and adoption of this feature.
class EnableLandlock {
 public:
  explicit EnableLandlock() = default;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_ALLOWLISTS_ENABLE_LANDLOCK_H_
