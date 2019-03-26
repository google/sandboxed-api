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

#include "sandboxed_api/sandbox2/mounts.h"

#include <unistd.h>

#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/sandbox2/testing.h"
#include "sandboxed_api/sandbox2/util/path.h"
#include "sandboxed_api/sandbox2/util/temp_file.h"
#include "sandboxed_api/util/status_matchers.h"

using sapi::IsOk;
using sapi::StatusIs;
using ::testing::Eq;

namespace sandbox2 {
namespace {

constexpr size_t kTmpfsSize = 1024;

TEST(MountTreeTest, TestInvalidFilenames) {
  Mounts mounts;

  EXPECT_THAT(mounts.AddFile(""), StatusIs(sapi::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddFile("a"),
              StatusIs(sapi::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddFileAt("/a", ""),
              StatusIs(sapi::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddFileAt("", "/a"),
              StatusIs(sapi::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddFileAt("/a", "a"),
              StatusIs(sapi::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddFile("/"),
              StatusIs(sapi::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddFileAt("/a", "/"),
              StatusIs(sapi::StatusCode::kInvalidArgument));
}

TEST(MountTreeTest, TestAddFile) {
  Mounts mounts;

  EXPECT_THAT(mounts.AddFile("/a"), IsOk());
  EXPECT_THAT(mounts.AddFile("/b"), IsOk());
  EXPECT_THAT(mounts.AddFile("/c/d"), IsOk());
  EXPECT_THAT(mounts.AddFile("/c/e"), IsOk());
  EXPECT_THAT(mounts.AddFile("/c/dd/e"), IsOk());

  EXPECT_THAT(mounts.AddFileAt("/a", "/f"), IsOk());
}

TEST(MountTreeTest, TestAddDir) {
  Mounts mounts;

  EXPECT_THAT(mounts.AddDirectoryAt("/a", "/a"), IsOk());
  EXPECT_THAT(mounts.AddDirectoryAt("/c/d", "/c/d"), IsOk());
  EXPECT_THAT(mounts.AddDirectoryAt("/c/d/e", "/c/d/e"), IsOk());
}

TEST(MountTreeTest, TestAddTmpFs) {
  Mounts mounts;

  EXPECT_THAT(mounts.AddTmpfs("/a", kTmpfsSize), IsOk());
  EXPECT_THAT(mounts.AddTmpfs("/a/b", kTmpfsSize), IsOk());
  EXPECT_THAT(mounts.AddFile("/a/b/c"), IsOk());
  EXPECT_THAT(mounts.AddDirectoryAt("/a/b/d", "/a/b/d"), IsOk());
}

TEST(MountTreeTest, TestMultipleInsertionFileSymlink) {
  Mounts mounts;

  auto result_or = CreateNamedTempFileAndClose(
      file::JoinPath(GetTestTempPath(), "testdir_"));
  ASSERT_THAT(result_or.status(), IsOk());
  std::string path = result_or.ValueOrDie();
  result_or = CreateNamedTempFileAndClose(
      file::JoinPath(GetTestTempPath(), "testdir_"));
  ASSERT_THAT(result_or.status(), IsOk());
  std::string symlink_path = result_or.ValueOrDie();
  ASSERT_THAT(unlink(symlink_path.c_str()), Eq(0));
  ASSERT_THAT(symlink(path.c_str(), symlink_path.c_str()), Eq(0));
  EXPECT_THAT(mounts.AddFileAt(path, "/a"), IsOk());
  EXPECT_THAT(mounts.AddFileAt(path, "/a"), IsOk());
  EXPECT_THAT(mounts.AddFileAt(symlink_path, "/a"), IsOk());
}

TEST(MountTreeTest, TestMultipleInsertionDirSymlink) {
  Mounts mounts;

  auto result_or = CreateTempDir(file::JoinPath(GetTestTempPath(), "testdir_"));
  ASSERT_THAT(result_or.status(), IsOk());
  std::string path = result_or.ValueOrDie();
  result_or = CreateNamedTempFileAndClose(
      file::JoinPath(GetTestTempPath(), "testdir_"));
  ASSERT_THAT(result_or.status(), IsOk());
  std::string symlink_path = result_or.ValueOrDie();
  ASSERT_THAT(unlink(symlink_path.c_str()), Eq(0));
  ASSERT_THAT(symlink(path.c_str(), symlink_path.c_str()), Eq(0));
  EXPECT_THAT(mounts.AddDirectoryAt(path, "/a"), IsOk());
  EXPECT_THAT(mounts.AddDirectoryAt(path, "/a"), IsOk());
  EXPECT_THAT(mounts.AddDirectoryAt(symlink_path, "/a"), IsOk());
  EXPECT_THAT(mounts.AddDirectoryAt(symlink_path, "/a"), IsOk());
}

TEST(MountTreeTest, TestMultipleInsertion) {
  Mounts mounts;

  EXPECT_THAT(mounts.AddFile("/c/d"), IsOk());

  EXPECT_THAT(mounts.AddFile("/c"),
              StatusIs(sapi::StatusCode::kFailedPrecondition));
  EXPECT_THAT(mounts.AddFileAt("/f", "/c"),
              StatusIs(sapi::StatusCode::kFailedPrecondition));
  EXPECT_THAT(mounts.AddDirectoryAt("/f", "/c"), IsOk());

  EXPECT_THAT(mounts.AddFile("/c/d/e"),
              StatusIs(sapi::StatusCode::kFailedPrecondition));
  EXPECT_THAT(mounts.AddFileAt("/f", "/c/d/e"),
              StatusIs(sapi::StatusCode::kFailedPrecondition));
  EXPECT_THAT(mounts.AddDirectoryAt("/f", "/c/d/e"),
              StatusIs(sapi::StatusCode::kFailedPrecondition));
}

TEST(MountTreeTest, TestEvilNullByte) {
  Mounts mounts;
  // create the filename with a null byte this way as g4 fix forces newlines
  // otherwise.
  std::string filename{"/a/b"};
  filename[2] = '\0';

  EXPECT_THAT(mounts.AddFile(filename),
              StatusIs(sapi::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddFileAt(filename, "/a"),
              StatusIs(sapi::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddFileAt("/a", filename),
              StatusIs(sapi::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddDirectoryAt(filename, "/a"),
              StatusIs(sapi::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddDirectoryAt("/a", filename),
              StatusIs(sapi::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddTmpfs(filename, kTmpfsSize),
              StatusIs(sapi::StatusCode::kInvalidArgument));
}

TEST(MountTreeTest, TestMinimalDynamicBinary) {
  Mounts mounts;
  EXPECT_THAT(mounts.AddMappingsForBinary(
                  GetTestSourcePath("sandbox2/testcases/minimal_dynamic")),
              IsOk());
  EXPECT_THAT(mounts.AddFile("/lib/x86_64-linux-gnu/libc.so.6"), IsOk());
}

}  // namespace
}  // namespace sandbox2
