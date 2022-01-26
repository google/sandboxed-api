// Copyright 2022 Google LLC
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

#include <fstream>

#include "../sandboxed.h"
#include "../utils/utils_zopfli.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sandboxed_api/util/temp_file.h"

using ::sapi::IsOk;

namespace {

std::string GetTestFilePath(const std::string& filename) {
  return sapi::file::JoinPath(getenv("TEST_FILES_DIR"), filename);
}

std::string GetTemporaryFile(const std::string& filename) {
  absl::StatusOr<std::string> tmp_file =
      sapi::CreateNamedTempFileAndClose(filename);
  if (!tmp_file.ok()) {
    return "";
  }

  return sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *tmp_file);
}

#define NAME DEFLATE
#define METHOD ZOPFLI_FORMAT_DEFLATE
#include "zopfli_template.c"
#undef NAME
#undef METHOD

#define NAME GZIP
#define METHOD ZOPFLI_FORMAT_GZIP
#include "zopfli_template.c"
#undef NAME
#undef METHOD

#define NAME ZLIB
#define METHOD ZOPFLI_FORMAT_ZLIB
#include "zopfli_template.c"
#undef NAME
#undef METHOD

}  // namespace
