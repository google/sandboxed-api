// Copyright 2020 Google LLC
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

#include "helper.h"  // NOLINT(build/include)

#include "../sandboxed.h"  // NOLINT(build/include)
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/sandbox2/util/path.h"

std::string GetFilePath(const absl::string_view filename) {
  std::string cwd = sandbox2::file_util::fileops::GetCWD();
  auto find = cwd.rfind("/build");
  if (find == std::string::npos) {
    LOG(ERROR)
        << "Something went wrong: CWD don't contain build dir. Please run "
           "tests from build dir. To run example send project dir as a "
           "parameter: ./sandboxed /absolute/path/to/project/dir .\n"
           "Falling back to using current working directory as root dir.\n";

    return sandbox2::file::JoinPath(cwd, "test", "images");
  }

  return sandbox2::file::JoinPath(cwd.substr(0, find), "test", "images",
                                  filename);
}

std::string GetFilePath(const absl::string_view dir,
                        const absl::string_view filename) {
  return sandbox2::file::JoinPath(dir, "test", "images", filename);
}
