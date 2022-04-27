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

// A binary to test network namespace hostname.
// Usage: ./hostname <expected hostname>
// Success only if the hostname is as expected.

#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printf("argc < 2\n");
    return EXIT_FAILURE;
  }

  char hostname[1024];
  if (gethostname(hostname, sizeof(hostname)) == -1) {
    printf("gethostname: error %d\n", errno);
    return EXIT_FAILURE;
  }

  if (strcmp(hostname, argv[1]) != 0) {
    printf("gethostname: got %s, want %s\n", hostname, argv[1]);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
