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

#ifndef SANDBOXED_API_TOOLS_FILEWRAPPER_FILEWRAPPER_H_
#define SANDBOXED_API_TOOLS_FILEWRAPPER_FILEWRAPPER_H_

#include <cctype>
#include <cstddef>
#include <string_view>

namespace sapi {

// Returns true if `ns` is a valid C++ namespace identifier.
// Valid namespaces are empty, or consist of identifiers separated by "::"
// where each identifier starts with [a-zA-Z_] and contains only [a-zA-Z0-9_].
inline bool IsValidNamespace(std::string_view ns) {
  if (ns.empty()) {
    return true;
  }
  size_t start = 0;
  while (start < ns.size()) {
    size_t end = ns.find("::", start);
    std::string_view segment = (end == std::string_view::npos)
                                   ? ns.substr(start)
                                   : ns.substr(start, end - start);
    if (segment.empty()) {
      return false;
    }
    if (!std::isalpha(static_cast<unsigned char>(segment[0])) &&
        segment[0] != '_') {
      return false;
    }
    for (char c : segment) {
      if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
        return false;
      }
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 2;
  }
  return true;
}

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_FILEWRAPPER_FILEWRAPPER_H_
