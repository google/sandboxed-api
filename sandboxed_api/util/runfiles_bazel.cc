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

#include <cstdlib>

#include "absl/strings/str_format.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/runfiles.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace sapi {

std::string GetDataDependencyFilePath(absl::string_view relative_path) {
  using bazel::tools::cpp::runfiles::Runfiles;

  static Runfiles* runfiles = []() {
    std::string error;
    auto* runfiles = Runfiles::Create(/*argv=*/"" /* unknown */, &error);
    SAPI_RAW_CHECK(runfiles != nullptr, error.c_str());

    // Setup environment for child processes.
    for (const auto& entry : runfiles->EnvVars()) {
      setenv(entry.first.c_str(), entry.second.c_str(), 1 /* overwrite */);
    }
    return runfiles;
  }();
  return runfiles->Rlocation(std::string(relative_path));
}

namespace internal {

std::string GetSapiDataDependencyFilePath(absl::string_view relative_path) {
  return GetDataDependencyFilePath(file::JoinPath(
      "com_google_sandboxed_api", "sandboxed_api", relative_path));
}

}  // namespace internal
}  // namespace sapi
