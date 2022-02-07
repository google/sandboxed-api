// Copyright 2022 Google LLC
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

#include <unistd.h>

#include <fstream>
#include <iostream>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "contrib/zopfli/sandboxed.h"
#include "contrib/zopfli/utils/utils_zopfli.h"

ABSL_FLAG(bool, zlib, false, "zlib compression");
ABSL_FLAG(bool, gzip, false, "gzip compression");

int main(int argc, char* argv[]) {
  std::string prog_name(argv[0]);
  google::InitGoogleLogging(argv[0]);
  std::vector<char*> args = absl::ParseCommandLine(argc, argv);

  if (args.size() != 3) {
    std::cerr << "Usage:\n  " << prog_name << " INPUT OUTPUT\n";
    return EXIT_FAILURE;
  }

  std::ifstream infile(args[1], std::ios::binary);
  if (!infile.is_open()) {
    std::cerr << "Unable to open " << args[1] << std::endl;
    return EXIT_FAILURE;
  }
  std::ofstream outfile(args[2], std::ios::binary);
  if (!outfile.is_open()) {
    std::cerr << "Unable to open " << args[2] << std::endl;
    return EXIT_FAILURE;
  }

  ZopfliSapiSandbox sandbox;
  if (!sandbox.Init().ok()) {
    std::cerr << "Unable to start sandbox\n";
    return EXIT_FAILURE;
  }

  ZopfliApi api(&sandbox);

  ZopfliFormat format = ZOPFLI_FORMAT_DEFLATE;
  if (absl::GetFlag(FLAGS_zlib)) {
    format = ZOPFLI_FORMAT_ZLIB;
  } else if (absl::GetFlag(FLAGS_gzip)) {
    format = ZOPFLI_FORMAT_GZIP;
  }

  absl::Status status = Compress(api, infile, outfile, format);
  if (!status.ok()) {
    std::cerr << "Unable to compress file.\n";
    std::cerr << status << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
