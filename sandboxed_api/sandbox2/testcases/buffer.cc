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

#include "sandboxed_api/sandbox2/buffer.h"

#include <cstdint>
#include <utility>

int main(int argc, char* argv[]) {
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
