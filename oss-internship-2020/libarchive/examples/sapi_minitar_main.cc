// Copyright 2020 Google LLC
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

// This file contains the main function from the original minitar example:
// https://github.com/libarchive/libarchive/blob/master/examples/minitar/minitar.c
// Most of the logic is the same, it was only simplified a bit since this is
// only used for the command line tool.
// No sandboxing takes place in this function.

#include <iostream>

#include "sapi_minitar.h"

static void usage(void);

int main(int argc, const char** argv) {
  google::InitGoogleLogging(argv[0]);
  const char* filename = NULL;
  int compress, flags, mode, opt;

  (void)argc;
  mode = 'x';
  int verbose = 0;
  compress = '\0';
  flags = ARCHIVE_EXTRACT_TIME;

  while (*++argv != NULL && **argv == '-') {
    const char* p = *argv + 1;

    while ((opt = *p++) != '\0') {
      switch (opt) {
        case 'c':
          mode = opt;
          break;
        case 'f':
          if (*p != '\0')
            filename = p;
          else
            filename = *++argv;
          p += strlen(p);
          break;
        case 'j':
          compress = opt;
          break;
        case 'p':
          flags |= ARCHIVE_EXTRACT_PERM;
          flags |= ARCHIVE_EXTRACT_ACL;
          flags |= ARCHIVE_EXTRACT_FFLAGS;
          break;
        case 't':
          mode = opt;
          break;
        case 'v':
          verbose++;
          break;
        case 'x':
          mode = opt;
          break;
        case 'y':
          compress = opt;
          break;
        case 'Z':
          compress = opt;
          break;
        case 'z':
          compress = opt;
          break;
        default:
          usage();
      }
    }
  }

  switch (mode) {
    case 'c':
      create(filename, compress, argv, verbose);
      break;
    case 't':
      extract(filename, 0, flags, verbose);
      break;
    case 'x':
      extract(filename, 1, flags, verbose);
      break;
  }

  return EXIT_SUCCESS;
}

static void usage(void) {
  /* Many program options depend on compile options. */
  const char* m =
      "Usage: minitar [-"
      "c"
      "j"
      "tvx"
      "y"
      "Z"
      "z"
      "] [-f file] [file]\n";

  std::cout << m << std::endl;
  exit(EXIT_FAILURE);
}
