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

// A binary that communicates with comms before being sandboxed.

#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/util/raw_logging.h"

int main(int argc, char* argv[]) {
  sandbox2::Comms comms(sandbox2::Comms::kDefaultConnection);

  // Exchange data with sandbox sandbox (parent) before sandboxing is enabled.
  SAPI_RAW_CHECK(comms.SendBool(true), "Sending data to the executor");

  sandbox2::Client client(&comms);
  client.SandboxMeHere();

  return 33;
}
