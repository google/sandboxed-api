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

#ifndef RASTER_TO_GTIFF_UTILS_H_
#define RASTER_TO_GTIFF_UTILS_H_

#include <optional>
#include <string>

#include "absl/strings/string_view.h"

namespace gdal::sandbox::utils {

// RAII wrapper that creates temporary file and automatically unlinks it
class TempFile {
 public:
  explicit TempFile(absl::string_view prefix);
  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;
  TempFile(TempFile&&) = delete;
  TempFile& operator=(TempFile&&) = delete;
  ~TempFile();

  bool HasValue() const;
  int GetFd() const;
  std::string GetPath() const;

 private:
  std::optional<std::pair<std::string, int>> file_data_ = std::nullopt;
};

// Helper function to retrieve potential proj.db path from environment variable
std::optional<std::string> FindProjDbPath();

// Tries to get test folder path from the TEST_SRCDIR environment variable and
// uses CWD path otherwise to join it with the testdata_path
std::string GetTestDataPath(absl::string_view testdata_path);

}  // namespace gdal::sandbox::utils

#endif  // RASTER_TO_GTIFF_UTILS_H_
