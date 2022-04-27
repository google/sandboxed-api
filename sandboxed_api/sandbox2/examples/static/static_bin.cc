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

// This file is an example of a binary which is intended to be sandboxed by the
// sandbox2. It's not google3-based, and compiled statically (see BUILD).
//
// It inverts all bytes coming from stdin and writes them to the stdout.

#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <cctype>
#include <cstddef>
#include <cstdio>

int main(int argc, char* argv[]) {
  char buf[1024];
  size_t total_bytes = 0U;

  prctl(PR_SET_NAME, "static_bin");

  fprintf(stderr, "=============================\n");
  fprintf(stderr, "Starting file capitalization\n");
  fprintf(stderr, "=============================\n");
  fflush(nullptr);

  for (;;) {
    ssize_t sz = read(STDIN_FILENO, buf, sizeof(buf));
    if (sz < 0) {
      perror("read");
      break;
    }
    if (sz == 0) {
      break;
    }
    for (int i = 0; i < sz; i++) {
      buf[i] = toupper(buf[i]);
    }
    write(STDOUT_FILENO, buf, sz);
    total_bytes += sz;
  }

  fprintf(stderr, "=============================\n");
  fprintf(stderr, "Converted: %zu bytes\n", total_bytes);
  fprintf(stderr, "=============================\n");
  fflush(nullptr);
  return 0;
}
