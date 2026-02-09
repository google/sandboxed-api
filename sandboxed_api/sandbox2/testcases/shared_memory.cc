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

// A binary to test sandbox2 shared memory.

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"

int main(int argc, char* argv[]) {
  sandbox2::Comms comms(sandbox2::Comms::kDefaultConnection);
  sandbox2::Client client(&comms);

  int testno = atoi(argv[1]);  // NOLINT
  switch (testno) {
    case 1:
      // Not sandboxed yet.
      client.SandboxMeHere();
      break;
    case 2:
      // Already sandboxed, nothing to do.
      break;
    default:
      printf("Unknown test: %d\n", testno);
      return EXIT_FAILURE;
  }

  auto res = client.GetSharedMemoryMapping();
  if (!res.ok()) {
    return EXIT_FAILURE;
  }
  uint8_t* data = (*res)->data();
  if (data[0] != 'Z') {
    return EXIT_FAILURE;
  }
  data[0] = 'A';
  return EXIT_SUCCESS;
}
