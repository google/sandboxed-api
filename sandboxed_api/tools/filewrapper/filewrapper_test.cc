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

#include <unistd.h>

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status_matchers.h"
#include "sandboxed_api/embed_file.h"
#include "sandboxed_api/embed_toc.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/tools/filewrapper/filewrapper_embedded.h"
#include "sandboxed_api/util/file_helpers.h"

namespace sapi {
namespace {

using ::absl_testing::IsOk;
using ::sapi::GetTestSourcePath;
using ::testing::Eq;
using ::testing::Ne;
using ::testing::NotNull;
using ::testing::StrEq;

TEST(FilewrapperTest, BasicFunctionality) {
  auto raw_toc = filewrapper_embedded_create();
  ASSERT_THAT(raw_toc, NotNull());
  sapi::EmbedToc toc = sapi::EmbedToc::From(*raw_toc);

  EXPECT_THAT(std::string(toc.name), StrEq("filewrapper_embedded.bin"));

  std::string contents;
  ASSERT_THAT(file::GetContents(
                  GetTestSourcePath(
                      "tools/filewrapper/testdata/filewrapper_embedded.bin"),
                  &contents, file::Defaults()),
              IsOk());

  EXPECT_THAT(std::string(toc.data), StrEq(contents));

  int fd = EmbedFile::instance()->GetFdForFileToc(toc);
  ASSERT_THAT(fd, Ne(-1));

  std::string materialized_contents(256, '\0');
  EXPECT_THAT(pread(fd, &materialized_contents[0], 256, 0), Eq(256));
  EXPECT_THAT(materialized_contents, StrEq(contents));
}

}  // namespace
}  // namespace sapi
