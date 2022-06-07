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

std::string GetFilePath(const std::string& filename) {
  const char* test_srcdir = std::getenv("TEST_SRCDIR");

  return sapi::file::JoinPath(
      test_srcdir == nullptr ? sapi::file_util::fileops::GetCWD() : test_srcdir,
      filename);
}
