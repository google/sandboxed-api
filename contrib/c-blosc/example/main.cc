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
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "contrib/c-blosc/sandboxed.h"
#include "contrib/c-blosc/utils/utils_blosc.h"

ABSL_FLAG(bool, decompress, false, "decompress");
ABSL_FLAG(int, clevel, 5, "compression level");
ABSL_FLAG(uint32_t, nthreads, 5, "number of threads");
ABSL_FLAG(std::string, compressor, "blosclz",
          "compressor engine. Available: blosclz, lz4, lz4hc, zlib, zstd");

absl::Status Stream(CbloscApi& api, std::string& infile_s,
                    std::string& outfile_s) {
  std::ifstream infile(infile_s, std::ios::binary);
  if (!infile.is_open()) {
    return absl::UnavailableError(absl::StrCat("Unable to open ", infile_s));
  }
  std::ofstream outfile(outfile_s, std::ios::binary);
  if (!outfile.is_open()) {
    return absl::UnavailableError(absl::StrCat("Unable to open ", outfile_s));
  }

  std::string compressor(absl::GetFlag(FLAGS_compressor));

  if (absl::GetFlag(FLAGS_decompress)) {
    return Decompress(api, infile, outfile, 5);
  }

  return Compress(api, infile, outfile, absl::GetFlag(FLAGS_clevel), compressor,
                  absl::GetFlag(FLAGS_nthreads));
}

int main(int argc, char* argv[]) {
  std::string prog_name(argv[0]);
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  std::vector<char*> args = absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  if (args.size() != 3) {
    std::cerr << "Usage:\n  " << prog_name << " INPUT OUTPUT\n";
    return EXIT_FAILURE;
  }

  CbloscSapiSandbox sandbox;
  if (!sandbox.Init().ok()) {
    std::cerr << "Unable to start sandbox\n";
    return EXIT_FAILURE;
  }
  CbloscApi api(&sandbox);

  if (absl::Status status = api.blosc_init(); !status.ok()) {
    std::cerr << "Unable to init library\n";
    std::cerr << status << std::endl;
    return EXIT_FAILURE;
  }

  std::string infile_s(args[1]);
  std::string outfile_s(args[2]);

  if (absl::Status status = Stream(api, infile_s, outfile_s); !status.ok()) {
    std::cerr << "Unable to ";
    std::cerr << (absl::GetFlag(FLAGS_decompress) ? "de" : "");
    std::cerr << "compress file\n";
    std::cerr << status << std::endl;
    return EXIT_FAILURE;
  }

  if (absl::Status status = api.blosc_destroy(); !status.ok()) {
    std::cerr << "Unable to uninitialize library\n";
    std::cerr << status << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
