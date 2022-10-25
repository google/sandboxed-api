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

#include "helpers.h"  // NOLINT(build/include)

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "sandboxed_api/util/temp_file.h"

std::vector<uint8_t> GenerateValues() {
  std::vector<uint8_t> image;
  image.reserve(kImgLen);

  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      image.push_back(255 * !(x & y));
      image.push_back(x ^ y);
      image.push_back(x | y);
      image.push_back(255);
    }
  }

  return image;
}

std::string CreateTempDirAtCWD() {
  std::string cwd = sapi::file_util::fileops::GetCWD();
  CHECK(!cwd.empty()) << "Could not get current working directory";
  cwd.append("/");

  absl::StatusOr<std::string> result = sapi::CreateTempDir(cwd);
  CHECK_OK(result) << "Could not create temporary directory";
  return result.value();
}
