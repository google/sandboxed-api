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

#include <unistd.h>

#include <cstdlib>

#include "absl/strings/strip.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/runfiles.h"

namespace sapi {

std::string GetDataDependencyFilePath(absl::string_view relative_path) {
  static std::string* base_dir = []() {
    std::string link_name(PATH_MAX, '\0');
    SAPI_RAW_PCHECK(
        readlink("/proc/self/exe", &link_name[0], link_name.size()) != -1, "");
    link_name.resize(link_name.find_first_of('\0'));
    return new std::string(file::SplitPath(link_name).first);
  }();
  absl::string_view resolved =
      absl::StripSuffix(*base_dir, file::SplitPath(relative_path).first);
  return file::JoinPath(resolved, relative_path);
}

namespace internal {

std::string GetSapiDataDependencyFilePath(absl::string_view relative_path) {
  // The Bazel version has an additional "com_google_sandboxed_api" path
  // component.
  return GetDataDependencyFilePath(
      file::JoinPath("sandboxed_api", relative_path));
}

}  // namespace internal
}  // namespace sapi
