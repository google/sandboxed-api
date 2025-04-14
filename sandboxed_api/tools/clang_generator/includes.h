// Copyright 2025 Google LLC
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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_INCLUDES_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_INCLUDES_H_

#include <string>

#include "absl/strings/string_view.h"
#include "clang/Basic/FileEntry.h"

namespace sapi {

// Struct to store the information about an include directive.
struct IncludeInfo {
  // The string of the include directive.
  std::string include;
  // The file entry of the included file.
  const clang::FileEntryRef file;
  // True, if the include is an angled include, false otherwise.
  bool is_angled;
  // True, if the include is a system header, false otherwise.
  bool is_system_header;
};

// Removes the hash location marker from the hash location string.
// Example:
//   "[...]clang_generator/test/test_include.h:33:9" changes to
//   "[...]clang_generator/test/test_include.h"
inline std::string RemoveHashLocationMarker(absl::string_view hash_loc) {
  const auto first_colon_pos = hash_loc.find(':');
  if (first_colon_pos == absl::string_view::npos) {
    return std::string(hash_loc);
  }
  return std::string(hash_loc.substr(0, first_colon_pos));
}

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_INCLUDES_H_
