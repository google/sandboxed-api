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

#include "utils.h"  // NOLINT(build/include)

#include <unistd.h>

#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/temp_file.h"

namespace gdal::sandbox::utils {

namespace {

constexpr char kProjDbEnvVariableName[] = "PROJ_DB_PATH";
inline constexpr absl::string_view kDefaultProjDbPath =
    "/usr/local/share/proj/proj.db";

}  // namespace

TempFile::TempFile(absl::string_view prefix) {
  auto file_data = sandbox2::CreateNamedTempFile(prefix);

  if (file_data.ok()) {
    file_data_ = std::move(file_data.value());
  }
}

TempFile::~TempFile() {
  if (HasValue()) {
    unlink(file_data_.value().first.c_str());
  }
}

bool TempFile::HasValue() const { return file_data_.has_value(); }

int TempFile::GetFd() const { return file_data_.value().second; }

std::string TempFile::GetPath() const { return file_data_.value().first; }

std::optional<std::string> FindProjDbPath() {
  std::string proj_db_path(kDefaultProjDbPath);

  if (const char* proj_db_env_var = std::getenv(kProjDbEnvVariableName);
      proj_db_env_var != nullptr) {
    proj_db_path = proj_db_env_var;
  }

  if (!sandbox2::file_util::fileops::Exists(proj_db_path, false)) {
    return std::nullopt;
  }

  return proj_db_path;
}

std::string GetTestDataPath(absl::string_view testdata_path) {
  const char* test_srcdir = std::getenv("TEST_SRCDIR");

  return sandbox2::file::JoinPath(test_srcdir == nullptr
                                      ? sandbox2::file_util::fileops::GetCWD()
                                      : test_srcdir,
                                  testdata_path);
}

}  // namespace gdal::sandbox::utils
