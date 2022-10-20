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

#include <fstream>
#include <iostream>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "contrib/zopfli/sandboxed.h"
#include "contrib/zopfli/utils/utils_zopfli.h"

ABSL_FLAG(bool, stream, false, "stream memory to sandbox");
ABSL_FLAG(bool, zlib, false, "zlib compression");
ABSL_FLAG(bool, gzip, false, "gzip compression");

absl::Status CompressMain(ZopfliApi& api, std::string& infile_s,
                          std::string& outfile_s, ZopfliFormat format) {
  std::ifstream infile(infile_s, std::ios::binary);
  if (!infile.is_open()) {
    return absl::UnavailableError(absl::StrCat("Unable to open ", infile_s));
  }
  std::ofstream outfile(outfile_s, std::ios::binary);
  if (!outfile.is_open()) {
    return absl::UnavailableError(absl::StrCat("Unable to open ", outfile_s));
  }

  return Compress(api, infile, outfile, format);
}

absl::Status CompressMainFD(ZopfliApi& api, std::string& infile_s,
                            std::string& outfile_s, ZopfliFormat format) {
  sapi::v::Fd infd(open(infile_s.c_str(), O_RDONLY));
  if (infd.GetValue() < 0) {
    return absl::UnavailableError(absl::StrCat("Unable to open ", infile_s));
  }

  sapi::v::Fd outfd(open(outfile_s.c_str(), O_WRONLY | O_CREAT));
  if (outfd.GetValue() < 0) {
    return absl::UnavailableError(absl::StrCat("Unable to open ", outfile_s));
  }

  return (CompressFD(api, infd, outfd, format));
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

  ZopfliSapiSandbox sandbox;
  if (!sandbox.Init().ok()) {
    std::cerr << "Unable to start sandbox\n";
    return EXIT_FAILURE;
  }
  std::string infile_s(args[1]);
  std::string outfile_s(args[2]);

  ZopfliApi api(&sandbox);

  ZopfliFormat format = ZOPFLI_FORMAT_DEFLATE;
  if (absl::GetFlag(FLAGS_zlib)) {
    format = ZOPFLI_FORMAT_ZLIB;
  } else if (absl::GetFlag(FLAGS_gzip)) {
    format = ZOPFLI_FORMAT_GZIP;
  }

  absl::Status status;
  if (absl::GetFlag(FLAGS_stream)) {
    status = CompressMain(api, infile_s, outfile_s, format);
  } else {
    status = CompressMainFD(api, infile_s, outfile_s, format);
  }

  if (!status.ok()) {
    std::cerr << "Unable to compress file.\n";
    std::cerr << status << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
