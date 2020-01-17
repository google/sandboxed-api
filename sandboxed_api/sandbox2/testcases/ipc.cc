// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// A binary that uses comms and client, to receive FDs by name, communicate
// with them, sandboxed or not.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "absl/strings/numbers.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/util/raw_logging.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    printf("argc < 2\n");
    return EXIT_FAILURE;
  }

  sandbox2::Comms sb_comms(sandbox2::Comms::kSandbox2ClientCommsFD);
  sandbox2::Client client(&sb_comms);

  int testno;
  SAPI_RAW_CHECK(absl::SimpleAtoi(argv[1], &testno), "testno is not a number");
  switch (testno) {
    case 1:
      break;
    case 2:
      client.SandboxMeHere();
      break;
    case 3:
      // In case 3, we're running without a mapped fd. This is to test that the
      // Client object parses the environment variable properly in that case.
      return EXIT_SUCCESS;
    default:
      printf("Unknown test: %d\n", testno);
      return EXIT_FAILURE;
  }
  int expected_fd = -1;
  if (argc >= 3) {
    SAPI_RAW_CHECK(absl::SimpleAtoi(argv[2], &expected_fd),
                   "expected_fd is not a number");
  }

  int fd = client.GetMappedFD("ipc_test");
  if (expected_fd >= 0 && fd != expected_fd) {
    fprintf(stderr, "error mapped fd not as expected, got: %d, want: %d", fd,
            expected_fd);
    return EXIT_FAILURE;
  }
  sandbox2::Comms comms(fd);
  std::string hello;
  if (!comms.RecvString(&hello)) {
    fputs("error on comms.RecvString(&hello)", stderr);
    return EXIT_FAILURE;
  }

  if (!comms.SendString("world")) {
    fputs("error on comms.SendString(\"world\")", stderr);
    return EXIT_FAILURE;
  }

  printf("OK: All tests went OK\n");
  return EXIT_SUCCESS;
}
