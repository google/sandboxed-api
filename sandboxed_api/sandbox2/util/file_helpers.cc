// Copyright 2019 Google LLC. All Rights Reserved.
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

#include "sandboxed_api/sandbox2/util/file_helpers.h"

#include <fstream>
#include <sstream>

#include "absl/strings/str_cat.h"

namespace sandbox2 {
namespace file {

const Options& Defaults() {
  static auto* instance = new Options{};
  return *instance;
}

sapi::Status GetContents(absl::string_view path, std::string* output,
                         const file::Options& options) {
  std::ifstream in_stream{std::string(path), std::ios_base::binary};
  std::ostringstream out_stream;
  out_stream << in_stream.rdbuf();
  if (!in_stream || !out_stream) {
    return sapi::Status{sapi::StatusCode::kUnknown,
                        absl::StrCat("Error during read: ", path)};
  }
  *output = out_stream.str();
  return sapi::OkStatus();
}

sapi::Status SetContents(absl::string_view path, absl::string_view content,
                         const file::Options& options) {
  std::ofstream out_stream{std::string(path),
                           std::ios_base::trunc | std::ios_base::binary};
  if (!out_stream) {
    return sapi::Status{sapi::StatusCode::kUnknown,
                        absl::StrCat("Failed to open file: ", path)};
  }
  out_stream.write(content.data(), content.size());
  if (!out_stream) {
    return sapi::Status{sapi::StatusCode::kUnknown,
                        absl::StrCat("Error during write: ", path)};
  }
  return sapi::OkStatus();
}

}  // namespace file
}  // namespace sandbox2
