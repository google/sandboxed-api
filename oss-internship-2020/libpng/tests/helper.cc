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

std::string GetImagesFolder() {
  std::string cwd = sandbox2::file_util::fileops::GetCWD();
  auto find = cwd.rfind("/build");
  if (find == std::string::npos) {
    LOG(ERROR) << "Something went wrong: CWD don't contain build dir. "
               << "Please run tests from build dir, path might be incorrect\n";

    return sandbox2::file::JoinPath(cwd, "images");
  }

  return sandbox2::file::JoinPath(cwd.substr(0, find), "images");
}

std::string GetTestFilePath(const std::string& filename) {
  static std::string* images_folder_path = nullptr;
  if (!images_folder_path) {
    images_folder_path = new std::string(GetImagesFolder());
  }
  return sandbox2::file::JoinPath(*images_folder_path, filename);
}

