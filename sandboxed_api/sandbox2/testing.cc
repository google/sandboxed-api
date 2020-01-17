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

#include "sandboxed_api/sandbox2/testing.h"

#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/util/path.h"

namespace sandbox2 {

std::string GetTestTempPath(absl::string_view name) {
  // When using Bazel, the environment variable TEST_TMPDIR is guaranteed to be
  // set.
  // See https://docs.bazel.build/versions/master/test-encyclopedia.html for
  // details.
  return file::JoinPath(getenv("TEST_TMPDIR"), name);
}

std::string GetTestSourcePath(absl::string_view name) {
  // Like in GetTestTempPath(), when using Bazel, the environment variable
  // TEST_SRCDIR is guaranteed to be set.
  return file::JoinPath(getenv("TEST_SRCDIR"),
                        "com_google_sandboxed_api/sandboxed_api", name);
}

}  // namespace sandbox2
