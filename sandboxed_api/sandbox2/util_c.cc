// Copyright 2025 Google LLC
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

#include "sandboxed_api/sandbox2/util_c.h"

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "sandboxed_api/sandbox2/util.h"

// Returns true if the current process is running inside Sandbox2.
bool IsRunningInSandbox2() {
  absl::StatusOr<bool> result = sandbox2::util::IsRunningInSandbox2();
  if (!result.ok()) {
    LOG(ERROR) << result.status();
    return false;
  }
  return *result;
}
