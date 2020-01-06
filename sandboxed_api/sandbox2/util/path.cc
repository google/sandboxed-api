// Copyright 2020 Google LLC. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sandboxed_api/sandbox2/util/path.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"

namespace sandbox2 {
namespace file {
namespace internal {

constexpr char kPathSeparator[] = "/";

std::string JoinPathImpl(std::initializer_list<absl::string_view> paths) {
  std::string result;
  for (const auto& path : paths) {
    if (path.empty()) {
      continue;
    }
    if (result.empty()) {
      absl::StrAppend(&result, path);
      continue;
    }
    const auto comp = absl::StripPrefix(path, kPathSeparator);
    if (absl::EndsWith(result, kPathSeparator)) {
      absl::StrAppend(&result, comp);
    } else {
      absl::StrAppend(&result, kPathSeparator, comp);
    }
  }
  return result;
}

}  // namespace internal

bool IsAbsolutePath(absl::string_view path) {
  return !path.empty() && path[0] == '/';
}

std::pair<absl::string_view, absl::string_view> SplitPath(
    absl::string_view path) {
  const auto pos = path.find_last_of('/');

  // Handle the case with no '/' in 'path'.
  if (pos == absl::string_view::npos) {
    return {path.substr(0, 0), path};
  }

  // Handle the case with a single leading '/' in 'path'.
  if (pos == 0) {
    return {path.substr(0, 1), absl::ClippedSubstr(path, 1)};
  }
  return {path.substr(0, pos), absl::ClippedSubstr(path, pos + 1)};
}

std::string CleanPath(const absl::string_view unclean_path) {
  auto path = std::string(unclean_path);
  const char* src = path.c_str();
  std::string::iterator dst = path.begin();

  // Check for absolute path and determine initial backtrack limit.
  const bool is_absolute_path = *src == '/';
  if (is_absolute_path) {
    *dst++ = *src++;
    while (*src == '/') {
      ++src;
    }
  }
  std::string::const_iterator backtrack_limit = dst;

  // Process all parts
  while (*src) {
    bool parsed = false;

    if (src[0] == '.') {
      //  1dot ".<whateverisnext>", check for END or SEP.
      if (src[1] == '/' || !src[1]) {
        if (*++src) {
          ++src;
        }
        parsed = true;
      } else if (src[1] == '.' && (src[2] == '/' || !src[2])) {
        // 2dot END or SEP (".." | "../<whateverisnext>").
        src += 2;
        if (dst != backtrack_limit) {
          // We can backtrack the previous part
          for (--dst; dst != backtrack_limit && dst[-1] != '/'; --dst) {
            // Empty.
          }
        } else if (!is_absolute_path) {
          // Failed to backtrack and we can't skip it either. Rewind and copy.
          src -= 2;
          *dst++ = *src++;
          *dst++ = *src++;
          if (*src) {
            *dst++ = *src;
          }
          // We can never backtrack over a copied "../" part so set new limit.
          backtrack_limit = dst;
        }
        if (*src) {
          ++src;
        }
        parsed = true;
      }
    }

    // If not parsed, copy entire part until the next SEP or EOS.
    if (!parsed) {
      while (*src && *src != '/') {
        *dst++ = *src++;
      }
      if (*src) {
        *dst++ = *src++;
      }
    }

    // Skip consecutive SEP occurrences.
    while (*src == '/') {
      ++src;
    }
  }

  // Calculate and check the length of the cleaned path.
  int path_length = dst - path.begin();
  if (path_length != 0) {
    // Remove trailing '/' except if it is root path ("/" ==> path_length := 1).
    if (path_length > 1 && path[path_length - 1] == '/') {
      --path_length;
    }
    path.resize(path_length);
  } else {
    // The cleaned path is empty; assign "." as per the spec.
    path.assign(1, '.');
  }
  return path;
}

}  // namespace file
}  // namespace sandbox2
