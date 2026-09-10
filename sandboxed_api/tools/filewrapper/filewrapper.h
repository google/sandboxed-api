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

#include <algorithm>
#include <iterator>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace sapi {

inline bool IsCppKeyword(absl::string_view s) {
  static constexpr absl::string_view kKeywords[] = {
      "alignas",
      "alignof",
      "and",
      "and_eq",
      "asm",
      "atomic_cancel",
      "atomic_commit",
      "atomic_noexcept",
      "auto",
      "bitand",
      "bitor",
      "bool",
      "break",
      "case",
      "catch",
      "char",
      "char16_t",
      "char32_t",
      "char8_t",
      "class",
      "co_await",
      "co_return",
      "co_yield",
      "compl",
      "concept",
      "const",
      "const_cast",
      "consteval",
      "constexpr",
      "constinit",
      "continue",
      "decltype",
      "default",
      "delete",
      "do",
      "double",
      "dynamic_cast",
      "else",
      "enum",
      "explicit",
      "export",
      "extern",
      "false",
      "float",
      "for",
      "friend",
      "goto",
      "if",
      "inline",
      "int",
      "long",
      "mutable",
      "namespace",
      "new",
      "noexcept",
      "not",
      "not_eq",
      "nullptr",
      "operator",
      "or",
      "or_eq",
      "private",
      "protected",
      "public",
      "reflexpr",
      "register",
      "reinterpret_cast",
      "requires",
      "return",
      "short",
      "signed",
      "sizeof",
      "static",
      "static_assert",
      "static_cast",
      "struct",
      "switch",
      "synchronized",
      "template",
      "this",
      "thread_local",
      "throw",
      "true",
      "try",
      "typedef",
      "typeid",
      "typename",
      "union",
      "unsigned",
      "using",
      "virtual",
      "void",
      "volatile",
      "wchar_t",
      "while",
      "xor",
      "xor_eq",
  };
  return std::binary_search(std::begin(kKeywords), std::end(kKeywords), s);
}

inline bool IsValidCppIdentifier(absl::string_view ident) {
  if (ident.empty()) {
    return false;
  }
  if (!absl::ascii_isalpha(ident[0]) && ident[0] != '_') {
    return false;
  }
  for (char c : ident) {
    if (!absl::ascii_isalnum(c) && c != '_') {
      return false;
    }
  }
  return !IsCppKeyword(ident);
}

inline bool IsValidNamespace(absl::string_view ns) {
  if (ns.empty()) {
    return false;
  }
  std::vector<absl::string_view> parts = absl::StrSplit(ns, "::");
  for (absl::string_view part : parts) {
    if (!IsValidCppIdentifier(part)) {
      return false;
    }
  }
  return true;
}

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_FILEWRAPPER_FILEWRAPPER_H_