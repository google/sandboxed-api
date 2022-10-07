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

// A binary that uses a buffer from its executor.

#include <cstdio>
#include <cstdlib>

#include "absl/strings/str_format.h"
#include "sandboxed_api/sandbox2/buffer.h"
#include "sandboxed_api/sandbox2/comms.h"

int main(int argc, char* argv[]) {
  if (argc != 2) {
    absl::FPrintF(stderr, "argc != 2\n");
    return EXIT_FAILURE;
  }

  int testno = atoi(argv[1]);  // NOLINT
  switch (testno) {
    case 1: {  // Dup and map to static FD
      auto buffer_or = sandbox2::Buffer::CreateFromFd(3);
      if (!buffer_or.ok()) {
        return EXIT_FAILURE;
      }
      auto buffer = std::move(buffer_or).value();
      uint8_t* buf = buffer->data();
      // Test that we can read data from the executor.
      if (buf[0] != 'A') {
        return EXIT_FAILURE;
      }
      // Test that we can write data to the executor.
      buf[buffer->size() - 1] = 'B';
      return EXIT_SUCCESS;
    }

    case 2: {  // Send and receive FD
      sandbox2::Comms comms(sandbox2::Comms::kDefaultConnection);
      int fd;
      if (!comms.RecvFD(&fd)) {
        return EXIT_FAILURE;
      }
      auto buffer_or = sandbox2::Buffer::CreateFromFd(fd);
      if (!buffer_or.ok()) {
        return EXIT_FAILURE;
      }
      auto buffer = std::move(buffer_or).value();
      uint8_t* buf = buffer->data();
      // Test that we can read data from the executor.
      if (buf[0] != 'A') {
        return EXIT_FAILURE;
      }
      // Test that we can write data to the executor.
      buf[buffer->size() - 1] = 'B';
      return EXIT_SUCCESS;
    }

    default:
      absl::FPrintF(stderr, "Unknown test: %d\n", testno);
  }
  return EXIT_FAILURE;
}
