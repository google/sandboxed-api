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

#include "sandboxed_api/sandbox2/util.h"

#include <unistd.h>

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/path.h"

namespace sandbox2::util {
namespace {

using ::sapi::GetTestTempPath;
using ::testing::Gt;
using ::testing::IsTrue;

constexpr char kTestDir[] = "a/b/c";

TEST(UtilTest, TestCreateDirSuccess) {
  EXPECT_THAT(CreateDirRecursive(GetTestTempPath(kTestDir), 0700), IsTrue());
}

TEST(UtilTest, TestCreateDirExistSuccess) {
  const std::string test_dir = GetTestTempPath(kTestDir);
  EXPECT_THAT(CreateDirRecursive(test_dir, 0700), IsTrue());
  EXPECT_THAT(CreateDirRecursive(test_dir, 0700), IsTrue());
}

TEST(UtilTest, TestCreateMemFd) {
  int fd = 0;
  ASSERT_THAT(CreateMemFd(&fd), IsTrue());
  EXPECT_THAT(fd, Gt(1));
  close(fd);
}

}  // namespace
}  // namespace sandbox2::util
