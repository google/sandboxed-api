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

#ifndef SANDBOXED_API_SANDBOX2_UTIL_PATH_H_
#define SANDBOXED_API_SANDBOX2_UTIL_PATH_H_

#include <initializer_list>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"

namespace sapi::file {

namespace internal {
// Not part of the public API.
std::string JoinPathImpl(std::initializer_list<absl::string_view> paths);
}  // namespace internal

// Joins multiple paths together using the platform-specific path separator.
// Arguments must be convertible to absl::string_view.
template <typename... T>
inline std::string JoinPath(const T&... args) {
  return internal::JoinPathImpl({args...});
}

// Return true if path is absolute.
bool IsAbsolutePath(absl::string_view path);

// Returns the parts of the path, split on the final "/".  If there is no
// "/" in the path, the first part of the output is empty and the second
// is the input. If the only "/" in the path is the first character, it is
// the first part of the output.
std::pair<absl::string_view, absl::string_view> SplitPath(
    absl::string_view path);

// Collapses duplicate "/"s, resolve ".." and "." path elements, removes
// trailing "/".
//
// NOTE: This respects relative vs. absolute paths, but does not
// invoke any system calls in order to resolve relative paths to the actual
// working directory. That is, this is purely a string manipulation, completely
// independent of process state.
std::string CleanPath(absl::string_view path);

}  // namespace sapi::file

#endif  // SANDBOXED_API_SANDBOX2_UTIL_PATH_H_
