// Copyright 2019 Google LLC. All Rights Reserved.
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
#include <unistd.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/sandbox2/testing.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/sandbox2/util/path.h"
#include "sandboxed_api/util/status_matchers.h"

using sapi::IsOk;
using testing::Eq;
using testing::IsTrue;
using testing::Ne;
using testing::StartsWith;

namespace sandbox2 {
namespace {

TEST(TempFileTest, CreateTempDirTest) {
  const std::string prefix = GetTestTempPath("MakeTempDirTest_");
  auto result_or = CreateTempDir(prefix);
  ASSERT_THAT(result_or.status(), IsOk());
  std::string path = result_or.ValueOrDie();
  EXPECT_THAT(path, StartsWith(prefix));
  EXPECT_THAT(file_util::fileops::Exists(path, false), IsTrue());
  result_or = CreateTempDir("non_existing_dir/prefix");
  EXPECT_THAT(result_or, StatusIs(sapi::StatusCode::kUnknown));
}

TEST(TempFileTest, MakeTempFileTest) {
  const std::string prefix = GetTestTempPath("MakeTempDirTest_");
  auto result_or = CreateNamedTempFile(prefix);
  ASSERT_THAT(result_or.status(), IsOk());
  std::string path;
  int fd;
  std::tie(path, fd) = result_or.ValueOrDie();
  EXPECT_THAT(path, StartsWith(prefix));
  EXPECT_THAT(file_util::fileops::Exists(path, false), IsTrue());
  EXPECT_THAT(fcntl(fd, F_GETFD), Ne(-1));
  EXPECT_THAT(close(fd), Eq(0));
  result_or = CreateNamedTempFile("non_existing_dir/prefix");
  EXPECT_THAT(result_or, StatusIs(sapi::StatusCode::kUnknown));
}

}  // namespace
}  // namespace sandbox2
