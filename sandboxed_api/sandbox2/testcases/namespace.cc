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

// Checks various things related to namespaces, depending on the first argument:
// ./binary 0 <file1> <file2> ... <fileN>:
//    Make sure all provided files exist and are RO, return 0 on OK.
//    Returns the index of the first non-existing file on failure.
// ./binary 1 <file1> <file2> ... <fileN>:
//    Make sure all provided files exist and are RW, return 0 on OK.
//    Returns the index of the first non-existing file on failure.
// ./binary 2
//    Make sure that we run in a PID namespace (this implies getpid() == 1)
//    Returns 0 on OK.
// ./binary 3 <uid> <gid>
//    Make sure getuid()/getgid() returns the provided uid/gid (User namespace).
//    Returns 0 on OK.
// ./binary 4 <file1> <file2> ... <fileN>:
//    Create provided files, return 0 on OK.
//    Returns the index of the first non-creatable file on failure.
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdlib>

int main(int argc, char* argv[]) {
  if (argc < 2) {
    return 0;
  }

  int mode = atoi(argv[1]);  // NOLINT(runtime/deprecated_fn)

  switch (mode) {
    case 0: {
      // Make sure file exist
      for (int i = 2; i < argc; i++) {
        if (access(argv[i], R_OK)) {
          return i - 1;
        }
      }
    } break;

    case 1: {
      for (int i = 2; i < argc; i++) {
        if (access(argv[i], W_OK)) {
          return i - 1;
        }
      }
    } break;

    case 2: {
      if (getpid() != 2) {
        return -1;
      }
    } break;

    case 3: {
      if (argc != 4) {
        return 1;
      }

      if (getuid() != atoi(argv[2])        // NOLINT(runtime/deprecated_fn)
          || getgid() != atoi(argv[3])) {  // NOLINT(runtime/deprecated_fn)
        return -1;
      }
    } break;

    case 4:
      for (int i = 2; i < argc; ++i) {
        if (open(argv[i], O_CREAT | O_WRONLY, 0644) == -1) {
          return i - 1;
        }
      }
      break;

    default:
      return 1;
  }

  return 0;
}
