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

// #include "sandboxed_api/util/flag.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "contrib/miniz/utils/utils_miniz.h"

ABSL_FLAG(bool, decompress, false, "decompress");
ABSL_FLAG(uint32_t, level, 0, "compression level");

static absl::Status Stream(miniz_sapi::MinizApi& api, std::string infile_s,
                           std::string outfile_s) {
  std::ifstream infile(infile_s, std::ios::binary);
  if (!infile.is_open()) {
    return absl::UnavailableError(absl::StrCat("Unable to open ", infile_s));
  }
  std::ofstream outfile(outfile_s, std::ios::binary);
  if (!outfile.is_open()) {
    return absl::UnavailableError(absl::StrCat("Unable to open ", outfile_s));
  }

  SAPI_ASSIGN_OR_RETURN(auto vec, sapi::util::ReadFile(infile));

  std::cerr << "Testing!" << std::endl;
  auto v = (absl::GetFlag(FLAGS_decompress)
                ? sapi::util::DecompressInMemory(api, vec.data(), vec.size())
                : sapi::util::CompressInMemory(api, vec.data(), vec.size(),
                                               absl::GetFlag(FLAGS_level)));
  std::cerr << "Testing 2!" << std::endl;
  SAPI_ASSIGN_OR_RETURN(const std::vector<uint8_t> out, v);
  std::cerr << "Testing 3!" << std::endl;
  outfile.write(reinterpret_cast<const char*>(out.data()), out.size());
  if (!outfile.good()) {
    return absl::UnavailableError("Unable to write file");
  }

  return absl::OkStatus();
}

int main(int argc, char* argv[]) {
  if (argc == 0 || argv[0][0] == 0) return 1;  // caller bug
  std::string prog_name(argv[0]);
  google::InitGoogleLogging(argv[0]);
  absl::SetProgramUsageMessage("Trivial example using SAPI and miniz");
  std::vector<char*> args = absl::ParseCommandLine(argc, argv);

  if (args.size() != 3) {
    std::cerr << "Usage:\n  " << prog_name << " INPUT OUTPUT\n";
    return EXIT_FAILURE;
  }

  miniz_sapi::MinizSandbox sandbox;
  if (!sandbox.Init().ok()) {
    std::cerr << "Unable to start sandbox\n";
    return EXIT_FAILURE;
  }

  miniz_sapi::MinizApi api(&sandbox);

  absl::Status status;
  status = Stream(api, args[1], args[2]);

  if (!status.ok()) {
    std::cerr << "Unable to ";
    std::cerr << (absl::GetFlag(FLAGS_decompress) ? "decompress" : "compress");
    std::cerr << " file.\n" << status << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
