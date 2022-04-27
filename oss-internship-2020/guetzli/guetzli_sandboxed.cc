// Copyright 2020 Google LLC
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

#include <cstdio>
#include <iostream>

#include "guetzli_transaction.h"  // NOLINT(build/include)
#include "sandboxed_api/util/fileops.h"

namespace {

constexpr int kDefaultJPEGQuality = 95;
constexpr int kDefaultMemlimitMB = 6000;

void Usage() {
  fprintf(stderr,
          "Guetzli JPEG compressor. Usage: \n"
          "guetzli [flags] input_filename output_filename\n"
          "\n"
          "Flags:\n"
          "  --verbose    - Print a verbose trace of all attempts to standard "
          "output.\n"
          "  --quality Q  - Visual quality to aim for, expressed as a JPEG "
          "quality value.\n"
          "                 Default value is %d.\n"
          "  --memlimit M - Memory limit in MB. Guetzli will fail if unable to "
          "stay under\n"
          "                 the limit. Default limit is %d MB.\n"
          "  --nomemlimit - Do not limit memory usage.\n",
          kDefaultJPEGQuality, kDefaultMemlimitMB);
  exit(1);
}

}  // namespace

int main(int argc, char* argv[]) {
  int verbose = 0;
  int quality = kDefaultJPEGQuality;
  int memlimit_mb = kDefaultMemlimitMB;

  int opt_idx = 1;
  for (; opt_idx < argc; opt_idx++) {
    if (strnlen(argv[opt_idx], 2) < 2 || argv[opt_idx][0] != '-' ||
        argv[opt_idx][1] != '-')
      break;

    if (!strcmp(argv[opt_idx], "--verbose")) {
      verbose = 1;
    } else if (!strcmp(argv[opt_idx], "--quality")) {
      opt_idx++;
      if (opt_idx >= argc) Usage();
      quality = atoi(argv[opt_idx]);  // NOLINT(runtime/deprecated_fn)
    } else if (!strcmp(argv[opt_idx], "--memlimit")) {
      opt_idx++;
      if (opt_idx >= argc) Usage();
      memlimit_mb = atoi(argv[opt_idx]);  // NOLINT(runtime/deprecated_fn)
    } else if (!strcmp(argv[opt_idx], "--nomemlimit")) {
      memlimit_mb = -1;
    } else if (!strcmp(argv[opt_idx], "--")) {
      opt_idx++;
      break;
    } else {
      fprintf(stderr, "Unknown commandline flag: %s\n", argv[opt_idx]);
      Usage();
    }
  }

  if (argc - opt_idx != 2) {
    Usage();
  }

  guetzli::sandbox::TransactionParams params = {
      argv[opt_idx], argv[opt_idx + 1], verbose, quality, memlimit_mb};

  guetzli::sandbox::GuetzliTransaction transaction(std::move(params));
  auto result = transaction.Run();

  if (!result.ok()) {
    std::cerr << result.ToString() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
