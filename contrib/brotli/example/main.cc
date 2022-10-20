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

#include <iostream>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "contrib/brotli/sandboxed.h"
#include "contrib/brotli/utils/utils_brotli.h"
#include "contrib/brotli/utils/utils_brotli_dec.h"
#include "contrib/brotli/utils/utils_brotli_enc.h"

ABSL_FLAG(bool, decompress, false, "decompress");

absl::Status CompressInMemory(BrotliSandbox& sandbox,
                              const std::string& in_file_s,
                              const std::string& out_file_s) {
  BrotliEncoder enc(&sandbox);
  if (!enc.IsInit()) {
    return absl::UnavailableError("Unable to init brotli encoder");
  }

  SAPI_ASSIGN_OR_RETURN(std::vector<uint8_t> buf_in, ReadFile(in_file_s));
  SAPI_RETURN_IF_ERROR(enc.Compress(buf_in));
  SAPI_ASSIGN_OR_RETURN(std::vector<uint8_t> buf_out, enc.TakeOutput());
  SAPI_RETURN_IF_ERROR(WriteFile(out_file_s, buf_out));

  return absl::OkStatus();
}

absl::Status DecompressInMemory(BrotliSandbox& sandbox,
                                const std::string& in_file_s,
                                const std::string& out_file_s) {
  BrotliDecoder dec(&sandbox);
  if (!dec.IsInit()) {
    return absl::UnavailableError("Unable to init brotli decoder");
  }

  SAPI_ASSIGN_OR_RETURN(std::vector<uint8_t> buf_in, ReadFile(in_file_s));
  SAPI_ASSIGN_OR_RETURN(BrotliDecoderResult ret, dec.Decompress(buf_in));
  if (ret != BROTLI_DECODER_RESULT_SUCCESS) {
    return absl::UnavailableError("Compressed file corrupt");
  }
  SAPI_ASSIGN_OR_RETURN(std::vector<uint8_t> buf_out, dec.TakeOutput());
  SAPI_RETURN_IF_ERROR(WriteFile(out_file_s, buf_out));

  return absl::OkStatus();
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

  std::string in_file_s(args[1]);
  std::string out_file_s(args[2]);

  BrotliSapiSandbox sandbox;
  if (!sandbox.Init().ok()) {
    std::cerr << "Unable to start sandbox\n";
    return EXIT_FAILURE;
  }

  absl::Status status;
  if (absl::GetFlag(FLAGS_decompress)) {
    status = DecompressInMemory(sandbox, in_file_s, out_file_s);
  } else {
    status = CompressInMemory(sandbox, in_file_s, out_file_s);
  }
  if (!status.ok()) {
    std::cerr << status << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
