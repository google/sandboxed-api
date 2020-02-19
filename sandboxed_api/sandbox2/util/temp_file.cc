// Copyright 2019 Google LLC
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

#include "sandboxed_api/sandbox2/util/temp_file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdlib>
#include <vector>

#include "absl/strings/str_cat.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/sandbox2/util/strerror.h"
#include "sandboxed_api/util/canonical_errors.h"

namespace sandbox2 {

namespace {
constexpr absl::string_view kMktempSuffix = "XXXXXX";
}  // namespace

sapi::StatusOr<std::pair<std::string, int>> CreateNamedTempFile(
    absl::string_view prefix) {
  std::string name_template = absl::StrCat(prefix, kMktempSuffix);
  int fd = mkstemp(&name_template[0]);
  if (fd < 0) {
    return sapi::UnknownError(absl::StrCat("mkstemp():", StrError(errno)));
  }
  return std::pair<std::string, int>{std::move(name_template), fd};
}

sapi::StatusOr<std::string> CreateNamedTempFileAndClose(
    absl::string_view prefix) {
  auto result_or = CreateNamedTempFile(prefix);
  if (result_or.ok()) {
    std::string path;
    int fd;
    std::tie(path, fd) = result_or.ValueOrDie();
    close(fd);
    return path;
  }
  return result_or.status();
}

sapi::StatusOr<std::string> CreateTempDir(absl::string_view prefix) {
  std::string name_template = absl::StrCat(prefix, kMktempSuffix);
  if (mkdtemp(&name_template[0]) == nullptr) {
    return sapi::UnknownError(absl::StrCat("mkdtemp():", StrError(errno)));
  }
  return name_template;
}

}  // namespace sandbox2
