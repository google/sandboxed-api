// Copyright 2019 Google LLC
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

#include "sandboxed_api/sandbox2/forkingclient.h"

#include <unistd.h>

#include <memory>

#include "absl/log/check.h"
#include "sandboxed_api/sandbox2/sanitizer.h"

namespace sandbox2 {

pid_t ForkingClient::WaitAndFork() {
  // We don't instantiate the Fork-Server until the first Fork() call takes
  // place (in order to conserve resources, and avoid calling Fork-Server
  // initialization routines).
  if (!fork_server_worker_) {
    sanitizer::WaitForSanitizer();
    // Perform that check once only, because it's quite CPU-expensive.
    int n = sanitizer::GetNumberOfThreads(getpid());
    CHECK_NE(n, -1) << "sanitizer::GetNumberOfThreads failed";
    CHECK_EQ(n, 1) << "Too many threads (" << n
                   << ") during sandbox2::Client::WaitAndFork()";
    fork_server_worker_ = std::make_unique<ForkServer>(comms_);
  }
  return fork_server_worker_->ServeRequest();
}

}  // namespace sandbox2
