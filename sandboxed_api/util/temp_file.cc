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

#include "sandboxed_api/util/temp_file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdlib>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/status_macros.h"

namespace sapi {

namespace {
constexpr absl::string_view kMktempSuffix = "XXXXXX";
}  // namespace

absl::StatusOr<std::pair<std::string, int>> CreateNamedTempFile(
    absl::string_view prefix) {
  std::string name_template = absl::StrCat(prefix, kMktempSuffix);
  int fd = mkstemp(&name_template[0]);
  if (fd < 0) {
    return absl::ErrnoToStatus(errno, "mkstemp()");
  }
  return std::pair<std::string, int>{std::move(name_template), fd};
}

absl::StatusOr<std::string> CreateNamedTempFileAndClose(
    absl::string_view prefix) {
  SAPI_ASSIGN_OR_RETURN(auto result, CreateNamedTempFile(prefix));
  close(result.second);
  return std::move(result.first);
}

absl::StatusOr<std::string> CreateTempDir(absl::string_view prefix) {
  std::string name_template = absl::StrCat(prefix, kMktempSuffix);
  if (mkdtemp(&name_template[0]) == nullptr) {
    return absl::ErrnoToStatus(errno, "mkdtemp()");
  }
  return name_template;
}

}  // namespace sapi
