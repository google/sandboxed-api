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

#include "sandboxed_api/sandbox2/version.h"

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace sandbox2 {

absl::StatusOr<ParsedVersion> ParsedVersion::ParseVersion(
    absl::string_view version_string) {
  // We need to parse the version string to separate the version number and the
  // build hash.
  // The version string format is one of the following:
  //  - "V<version_number>"
  //  - "<sha256sum>"
  //  - "<sha256sum>V<version_number>"
  // where version_number is formatted as "${year}${month}${2digit_counter}".
  if (!absl::StrContains(version_string, 'V')) {
    return ParsedVersion{.build_hash = std::string(version_string)};
  }

  auto version_pos = version_string.find('V');
  absl::string_view build_hash = version_string.substr(0, version_pos);
  absl::string_view version_number = version_string.substr(version_pos + 1);
  // Parse the version number.
  ParsedVersion out;
  if (!absl::SimpleAtoi(version_number, &out.version_number) ||
      out.version_number <= 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid version number: ", version_string));
  }

  out.build_hash = std::string(build_hash);
  return out;
}

absl::string_view GetVersion() {
  // The version string format is: "V<version_number>"
  // where version_number is formatted as "${year}${month}${2digit_counter}".
  static constexpr absl::string_view kSandboxVersion = "V20260701";
  return kSandboxVersion;
}

}  // namespace sandbox2
