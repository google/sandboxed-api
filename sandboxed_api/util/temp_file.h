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

#ifndef SANDBOXED_API_UTIL_TEMP_FILE_H_
#define SANDBOXED_API_UTIL_TEMP_FILE_H_

#include <string>

#include "absl/status/statusor.h"

namespace sapi {

// Creates a temporary file under a path starting with prefix. File is not
// unlinked and its path is returned together with an open fd.
absl::StatusOr<std::pair<std::string, int>> CreateNamedTempFile(
    absl::string_view prefix);

// Creates a temporary file under a path starting with prefix. File is not
// unlinked and its path is returned. FD of the created file is closed just
// after creation.
absl::StatusOr<std::string> CreateNamedTempFileAndClose(
    absl::string_view prefix);

// Creates a temporary directory under a path starting with prefix.
// Returns the path of the created directory.
absl::StatusOr<std::string> CreateTempDir(absl::string_view prefix);

}  // namespace sapi

#endif  // SANDBOXED_API_UTIL_TEMP_FILE_H_
