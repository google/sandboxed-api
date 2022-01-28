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

#include "../sandboxed.h"  // NOLINT(build/include)
#include "sandboxed_api/util/path.h"

std::string GetSourcePath() { return getenv("TEST_SRCDIR"); }

std::string GetFilePath(absl::string_view filename) {
  return sandbox2::file::JoinPath(GetSourcePath(), "images", filename);
}
