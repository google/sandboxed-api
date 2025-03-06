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

#include <fcntl.h>
#include <syscall.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>

#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/util.h"

int main(int argc, char** argv) {
  int testno = atoi(argv[1]);  // NOLINT
  if (testno == 1) {
    sandbox2::Comms comms(sandbox2::Comms::kSandbox2ClientCommsFD);
    sandbox2::Client client(&comms);
    client.SandboxMeHere();
  }
  int result =
      sandbox2::util::Syscall(__NR_execveat, AT_EMPTY_PATH, 0, 0, 0, 0);
  if (result != -1 || errno != EPERM) {
    printf("System call should have been blocked\n");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
