// Copyright 2020 Google LLC
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

#include "helper.h"  // NOLINT(build/include)

#include <iostream>

auto* g_in_dir = new std::string();

std::string GetCWD() {
  char cwd[PATH_MAX];
  getcwd(cwd, sizeof(cwd));
  return cwd;
}

std::string GetImagesDir() {
  std::string cwd = GetCWD();
  auto find = cwd.rfind("/build");
  if (find == std::string::npos) {
    std::cerr << "Something went wrong: CWD don't contain build dir. "
              << "Please run tests from build dir, path might be incorrect\n";

    return cwd + "/test/images";
  }

  return cwd.substr(0, find) + "/test/images";
}

std::string GetFilePath(const std::string& filename) {
  if (g_in_dir->empty()) {
    *g_in_dir = GetImagesDir();
  }
  return sandbox2::file::JoinPath(*g_in_dir, filename);
}
