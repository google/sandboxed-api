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

#include <miniz.h>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "contrib/miniz/miniz_sapi.sapi.h"
#pragma GCC diagnostic error "-Wmissing-prototypes"

namespace sapi {
namespace util {
static constexpr size_t kFileMaxSize = 1024 * 1024 * 1024;  // 1GB

std::streamsize GetStreamSize(std::ifstream& stream);

absl::StatusOr<std::vector<uint8_t>> CompressInMemory(miniz_sapi::MinizApi& api,
                                                      uint8_t* ptr, size_t len,
                                                      int level);
absl::StatusOr<std::vector<uint8_t>> ReadFile(std::ifstream& in_stream);

absl::StatusOr<std::vector<uint8_t>> DecompressInMemory(
    miniz_sapi::MinizApi& api, uint8_t* ptr, size_t len);
}  // namespace util
}  // namespace sapi
