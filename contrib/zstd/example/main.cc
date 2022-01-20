// Copyright 2022 Google LLC
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

#include <unistd.h>

#include <fstream>
#include <iostream>
#include <string>

#include <gflags/gflags.h>

#include "../sandboxed.h"
#include "../utils/utils_zstd.h"

DEFINE_bool(decompress, false,"decompress");
DEFINE_bool(memory_mode, false, "in memory operations");
DEFINE_int32(level, 0, "compression level");

int main(int argc, char* argv[]) {
  std::string prog_name(argv[0]);
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (argc != 3) {
    std::cerr << "Usage:\n  "
              << prog_name
              << " INPUT OUTPUT\n";
    return EXIT_FAILURE;
  }

  std::ifstream infile(argv[1], std::ios::binary);
  if (!infile.is_open()) {
    std::cerr << "Unable to open " << argv[0] << std::endl;
    return EXIT_FAILURE;
  }
  std::ofstream outfile(argv[2], std::ios::binary);
  if (!outfile.is_open()) {
    std::cerr << "Unable to open " << argv[1] << std::endl;
    return EXIT_FAILURE;
  }

  ZstdSapiSandbox sandbox;
  if (!sandbox.Init().ok()) {
    std::cerr << "Unable to start sandbox\n";
    return EXIT_FAILURE;
  }

  ZstdApi api(&sandbox);

  absl::Status status;
  if (FLAGS_memory_mode && FLAGS_decompress) {
    status = DecompressInMemory(api, infile, outfile);
  } else if (FLAGS_memory_mode && FLAGS_decompress) {
    status = CompressInMemory(api, infile, outfile, FLAGS_level);
  } else if (!FLAGS_memory_mode && FLAGS_decompress) {
    status = DecompressStream(api, infile, outfile);
  } else {
    status = CompressStream(api, infile, outfile, FLAGS_level);
  }

  if (!status.ok()) {
    std::cerr << "Unable to ";
    std::cerr << (FLAGS_decompress ? "decompress" : "compress");
    std::cerr << " file.\n" << status << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
