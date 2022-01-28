// Copyright 2019 Google LLC
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

#ifndef SANDBOXED_API_SANDBOX2_UTIL_MAPS_PARSER_H_
#define SANDBOXED_API_SANDBOX2_UTIL_MAPS_PARSER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/statusor.h"

namespace sandbox2 {

struct MapsEntry {
  uint64_t start;
  uint64_t end;
  bool is_readable;
  bool is_writable;
  bool is_executable;
  bool is_shared;
  uint64_t pgoff;
  int major;
  int minor;
  uint64_t inode;
  std::string path;
};

absl::StatusOr<std::vector<MapsEntry>> ParseProcMaps(
    const std::string& contents);

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_UTIL_MAPS_PARSER_H_
