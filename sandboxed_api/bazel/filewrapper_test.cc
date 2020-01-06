// Copyright 2020 Google LLC. All Rights Reserved.
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

#include <cstdint>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "absl/strings/string_view.h"
#include "sandboxed_api/bazel/filewrapper_embedded.h"
#include "sandboxed_api/sandbox2/testing.h"
#include "sandboxed_api/sandbox2/util/file_helpers.h"
#include "sandboxed_api/util/status_matchers.h"

using ::sandbox2::GetTestSourcePath;
using ::sapi::IsOk;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::StrEq;

namespace sapi {
namespace {

TEST(FilewrapperTest, BasicFunctionality) {
  const FileToc* toc = filewrapper_embedded_create();

  EXPECT_THAT(toc->name, StrEq("filewrapper_embedded.bin"));
  EXPECT_THAT(toc->size, Eq(256));

  std::string contents;
  ASSERT_THAT(sandbox2::file::GetContents(
                  GetTestSourcePath("bazel/testdata/filewrapper_embedded.bin"),
                  &contents, sandbox2::file::Defaults()),
              IsOk());
  EXPECT_THAT(std::string(toc->data, toc->size), StrEq(contents));

  ++toc;
  EXPECT_THAT(toc->name, IsNull());
}

}  // namespace
}  // namespace sapi
