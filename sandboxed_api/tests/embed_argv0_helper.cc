// Copyright 2026 Google LLC
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

// Helper for embed_data_test: materializes the unmapped test payload through
// EmbedFile and verifies its contents.
// Exit codes: 0 on success, 1 if no file descriptor could be created, 2 if the
// materialized contents do not match the expected payload.

#include <unistd.h>

#include <string>

#include "absl/strings/string_view.h"
#include "sandboxed_api/embed_file.h"
#include "sandboxed_api/tests/unmapped_embedded_data.h"

int main() {
  constexpr absl::string_view kExpectedPayload =
      "SAPI embedded test payload data 0123456789\n";
  int fd = sapi::EmbedFile::instance()->GetFdForFileToc(
      unmapped_embedded_data_create());
  if (fd < 0) {
    return 1;
  }
  // Read one byte more than expected to detect trailing data.
  std::string contents(kExpectedPayload.size() + 1, '\0');
  ssize_t read_bytes = pread(fd, contents.data(), contents.size(), 0);
  if (read_bytes < 0 ||
      absl::string_view(contents.data(), read_bytes) != kExpectedPayload) {
    return 2;
  }
  return 0;
}
