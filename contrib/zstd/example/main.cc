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

#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "contrib/zstd/sandboxed.h"
#include "contrib/zstd/utils/utils_zstd.h"

ABSL_FLAG(bool, stream, false, "stream data to sandbox");
ABSL_FLAG(bool, decompress, false, "decompress");
ABSL_FLAG(bool, memory_mode, false, "in memory operations");
ABSL_FLAG(uint32_t, level, 0, "compression level");

absl::Status Stream(ZstdApi& api, std::string infile_s, std::string outfile_s) {
  std::ifstream infile(infile_s, std::ios::binary);
  if (!infile.is_open()) {
    return absl::UnavailableError(absl::StrCat("Unable to open ", infile_s));
  }
  std::ofstream outfile(outfile_s, std::ios::binary);
  if (!outfile.is_open()) {
    return absl::UnavailableError(absl::StrCat("Unable to open ", outfile_s));
  }

  if (absl::GetFlag(FLAGS_memory_mode) && absl::GetFlag(FLAGS_decompress)) {
    return DecompressInMemory(api, infile, outfile);
  } else if (absl::GetFlag(FLAGS_memory_mode) &&
             !absl::GetFlag(FLAGS_decompress)) {
    return CompressInMemory(api, infile, outfile, absl::GetFlag(FLAGS_level));
  } else if (!absl::GetFlag(FLAGS_memory_mode) &&
             absl::GetFlag(FLAGS_decompress)) {
    return DecompressStream(api, infile, outfile);
  }

  return CompressStream(api, infile, outfile, absl::GetFlag(FLAGS_level));
}

absl::Status FileDescriptor(ZstdApi& api, std::string infile_s,
                            std::string outfile_s) {
  sapi::v::Fd infd(open(infile_s.c_str(), O_RDONLY));
  if (infd.GetValue() < 0) {
    return absl::UnavailableError(absl::StrCat(("Unable to open ", infile_s)));
  }
  sapi::v::Fd outfd(open(outfile_s.c_str(), O_WRONLY));
  if (outfd.GetValue() < 0) {
    return absl::UnavailableError(absl::StrCat(("Unable to open ", outfile_s)));
  }

  if (absl::GetFlag(FLAGS_memory_mode) && absl::GetFlag(FLAGS_decompress)) {
    return DecompressInMemoryFD(api, infd, outfd);
  } else if (absl::GetFlag(FLAGS_memory_mode) &&
             !absl::GetFlag(FLAGS_decompress)) {
    return CompressInMemoryFD(api, infd, outfd, absl::GetFlag(FLAGS_level));
  } else if (!absl::GetFlag(FLAGS_memory_mode) &&
             absl::GetFlag(FLAGS_decompress)) {
    return DecompressStreamFD(api, infd, outfd);
  }

  return CompressStreamFD(api, infd, outfd, absl::GetFlag(FLAGS_level));
}

int main(int argc, char* argv[]) {
  std::string prog_name(argv[0]);
  google::InitGoogleLogging(argv[0]);
  std::vector<char*> args = absl::ParseCommandLine(argc, argv);

  if (args.size() != 3) {
    std::cerr << "Usage:\n  " << prog_name << " INPUT OUTPUT\n";
    return EXIT_FAILURE;
  }

  ZstdSapiSandbox sandbox;
  if (!sandbox.Init().ok()) {
    std::cerr << "Unable to start sandbox\n";
    return EXIT_FAILURE;
  }

  ZstdApi api(&sandbox);

  absl::Status status;
  if (absl::GetFlag(FLAGS_stream)) {
    status = Stream(api, argv[1], argv[2]);
  } else {
    status = FileDescriptor(api, argv[1], argv[2]);
  }

  if (!status.ok()) {
    std::cerr << "Unable to ";
    std::cerr << (absl::GetFlag(FLAGS_decompress) ? "decompress" : "compress");
    std::cerr << " file.\n" << status << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
