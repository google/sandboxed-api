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

#include "sandboxed_api/util/path.h"

#include <deque>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"

namespace sapi::file {
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
  int dotdot_num = 0;
  std::deque<absl::string_view> parts;
  for (absl::string_view part :
       absl::StrSplit(unclean_path, '/', absl::SkipEmpty())) {
    if (part == "..") {
      if (parts.empty()) {
        ++dotdot_num;
      } else {
        parts.pop_back();
      }
    } else if (part != ".") {
      parts.push_back(part);
    }
  }
  if (absl::StartsWith(unclean_path, "/")) {
    if (parts.empty()) {
      return "/";
    }
    parts.push_front("");
  } else {
    for (; dotdot_num; --dotdot_num) {
      parts.push_front("..");
    }
    if (parts.empty()) {
      return ".";
    }
  }
  return absl::StrJoin(parts, "/");
}

}  // namespace sapi::file
