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
#include <unistd.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sapi {
namespace {

using ::testing::Eq;
using ::testing::IsTrue;
using ::testing::Ne;
using ::testing::StartsWith;

TEST(TempFileTest, CreateTempDirTest) {
  const std::string prefix = GetTestTempPath("MakeTempDirTest_");
  SAPI_ASSERT_OK_AND_ASSIGN(std::string path, CreateTempDir(prefix));

  EXPECT_THAT(path, StartsWith(prefix));
  EXPECT_THAT(file_util::fileops::Exists(path, false), IsTrue());
  EXPECT_THAT(CreateTempDir("non_existing_dir/prefix"),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST(TempFileTest, MakeTempFileTest) {
  const std::string prefix = GetTestTempPath("MakeTempDirTest_");

  auto result_or = CreateNamedTempFile(prefix);
  ASSERT_THAT(result_or.status(), IsOk());
  auto [path, fd] = std::move(result_or).value();

  EXPECT_THAT(path, StartsWith(prefix));
  EXPECT_THAT(file_util::fileops::Exists(path, false), IsTrue());
  EXPECT_THAT(fcntl(fd, F_GETFD), Ne(-1));
  EXPECT_THAT(close(fd), Eq(0));
  EXPECT_THAT(CreateNamedTempFile("non_existing_dir/prefix"),
              StatusIs(absl::StatusCode::kNotFound));
}

}  // namespace
}  // namespace sapi
