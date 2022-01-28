// Copyright 2020 Google LLC
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

#include <cstdlib>

#include "absl/base/attributes.h"
#include "sandboxed_api/sandbox2/fork_client.h"
#include "sandboxed_api/sandbox2/global_forkclient.h"

namespace sandbox2 {
void StartGlobalForkserverFromLibCtor() {
  if (!getenv(sandbox2::kForkServerDisableEnv)) {
    GlobalForkClient::ForceStart();
  }
}
}  // namespace sandbox2

// Run the ForkServer from the constructor, when no other threads are present.
// Because it's possible to start thread-inducing initializers before
// RunInitializers() (base/googleinit.h) it's not enough to just register
// a 0000_<name> initializer instead.
ABSL_ATTRIBUTE_UNUSED
__attribute__((constructor)) static void StartSandbox2Forkserver() {
  sandbox2::StartGlobalForkserverFromLibCtor();
}
