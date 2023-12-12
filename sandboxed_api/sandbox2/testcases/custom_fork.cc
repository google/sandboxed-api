// Copyright 2023 Google LLC
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

#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/forkingclient.h"
#include "sandboxed_api/util/raw_logging.h"

int main(int argc, char* argv[]) {
  sandbox2::Comms comms(sandbox2::Comms::kDefaultConnection);
  sandbox2::ForkingClient s2client(&comms);

  for (;;) {
    pid_t pid = s2client.WaitAndFork();
    if (pid == -1) {
      SAPI_RAW_LOG(FATAL, "Could not spawn a new sandboxee");
    }
    if (pid == 0) {
      // Start sandboxing here
      s2client.SandboxMeHere();
      return 0;
    }
  }
}
