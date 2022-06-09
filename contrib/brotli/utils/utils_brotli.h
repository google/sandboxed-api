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

#ifndef CONTRIB_BROTLI_UTILS_UTILS_BROTLI_H_
#define CONTRIB_BROTLI_UTILS_UTILS_BROTLI_H_

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

constexpr size_t kFileMaxSize = size_t{1} << 30;  // 1GiB

std::streamsize GetStreamSize(std::ifstream& stream);

absl::StatusOr<std::vector<uint8_t>> ReadFile(const std::string& in_file_s);

absl::Status WriteFile(const std::string& out_file_s,
                       const std::vector<uint8_t>& out_buf);

#endif  // CONTRIB_BROTLI_UTILS_UTILS_BROTLI_H_
