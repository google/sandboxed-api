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

#include "contrib/brotli/utils/utils_brotli.h"

std::streamsize GetStreamSize(std::ifstream& stream) {
  stream.seekg(0, std::ios_base::end);
  std::streamsize ssize = stream.tellg();
  stream.seekg(0, std::ios_base::beg);

  return ssize;
}

absl::StatusOr<std::vector<uint8_t>> ReadFile(const std::string& in_file_s) {
  std::ifstream in_file(in_file_s);
  if (!in_file.is_open()) {
    return absl::UnavailableError("File could not be opened");
  }

  std::streamsize ssize = GetStreamSize(in_file);
  if (ssize >= kFileMaxSize) {
    return absl::UnavailableError("Incorrect size of file");
  }

  std::vector<uint8_t> out_buf(ssize);
  in_file.read(reinterpret_cast<char*>(out_buf.data()), ssize);
  if (ssize != in_file.gcount()) {
    return absl::UnavailableError("Premature end of file");
  }
  if (in_file.fail() || in_file.eof()) {
    return absl::UnavailableError("Error reading file");
  }

  return out_buf;
}

absl::Status WriteFile(const std::string& out_file_s,
                       const std::vector<uint8_t>& out_buf) {
  std::ofstream out_file(out_file_s);
  if (!out_file.is_open()) {
    return absl::UnavailableError("File could not be opened");
  }

  out_file.write(reinterpret_cast<const char*>(out_buf.data()), out_buf.size());
  if (!out_file.good()) {
    return absl::UnavailableError("Error writting file");
  }

  return absl::OkStatus();
}
