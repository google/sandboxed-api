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

#include <cstdlib>

#include "absl/strings/str_format.h"
#include "sandboxed_api/sandbox2/util/path.h"
#include "sandboxed_api/sandbox2/util/runfiles.h"
#include "sandboxed_api/util/flag.h"
#include "sandboxed_api/util/raw_logging.h"
#include "tools/cpp/runfiles/runfiles.h"

using bazel::tools::cpp::runfiles::Runfiles;

namespace sandbox2 {

std::string GetDataDependencyFilePath(absl::string_view relative_path) {
  static Runfiles* runfiles = []() {
    std::string error;
    auto* runfiles = Runfiles::Create(gflags::GetArgv0(), &error);
    SAPI_RAW_CHECK(runfiles != nullptr, "%s", error);

    // Setup environment for child processes.
    for (const auto& entry : runfiles->EnvVars()) {
      setenv(entry.first.c_str(), entry.second.c_str(), 1 /* overwrite */);
    }
    return runfiles;
  }();
  return runfiles->Rlocation(std::string(relative_path));
}

std::string GetInternalDataDependencyFilePath(absl::string_view relative_path) {
  return GetDataDependencyFilePath(
      file::JoinPath("com_google_sandboxed_api/sandboxed_api", relative_path));
}

}  // namespace sandbox2
