// Copyright 2026 Google LLC
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

#ifndef SANDBOXED_API_SANDBOX2_VERSION_H_
#define SANDBOXED_API_SANDBOX2_VERSION_H_

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace sandbox2 {

struct ParsedVersion {
  // Parses a version string formatted with one of the following formats:
  //  - "V<version_number>"
  //  - "<sha256sum>"
  //  - "<sha256sum>V<version_number>"
  static absl::StatusOr<ParsedVersion> ParseVersion(
      absl::string_view version_string);

  int version_number = 0;  // 0 represents unparsed or legacy V1
  std::string build_hash;
};

absl::string_view GetVersion();

inline constexpr int kProtobufConfigVersion = 20260702;

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_VERSION_H_
