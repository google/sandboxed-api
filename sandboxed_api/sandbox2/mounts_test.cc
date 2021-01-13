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

#include "sandboxed_api/sandbox2/mounts.h"

#include <unistd.h>

#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/file_helpers.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sandboxed_api/util/temp_file.h"

namespace sandbox2 {
namespace {

namespace file = ::sapi::file;
using ::sapi::CreateNamedTempFileAndClose;
using ::sapi::CreateTempDir;
using ::sapi::GetTestSourcePath;
using ::sapi::GetTestTempPath;
using ::sapi::IsOk;
using ::sapi::StatusIs;
using ::testing::Eq;
using ::testing::UnorderedElementsAreArray;

constexpr size_t kTmpfsSize = 1024;

TEST(MountTreeTest, TestInvalidFilenames) {
  Mounts mounts;

  EXPECT_THAT(mounts.AddFile(""), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddFile("a"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddFileAt("/a", ""),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddFileAt("", "/a"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddFileAt("/a", "a"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddFile("/"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddFileAt("/a", "/"),
              StatusIs(absl::StatusCode::kInvalidArgument));
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

  SAPI_ASSERT_OK_AND_ASSIGN(std::string path,
                       CreateNamedTempFileAndClose(
                           file::JoinPath(GetTestTempPath(), "testdir_")));
  SAPI_ASSERT_OK_AND_ASSIGN(std::string symlink_path,
                       CreateNamedTempFileAndClose(
                           file::JoinPath(GetTestTempPath(), "testdir_")));

  ASSERT_THAT(unlink(symlink_path.c_str()), Eq(0));
  ASSERT_THAT(symlink(path.c_str(), symlink_path.c_str()), Eq(0));

  EXPECT_THAT(mounts.AddFileAt(path, "/a"), IsOk());
  EXPECT_THAT(mounts.AddFileAt(path, "/a"), IsOk());
  EXPECT_THAT(mounts.AddFileAt(symlink_path, "/a"), IsOk());
}

TEST(MountTreeTest, TestMultipleInsertionDirSymlink) {
  Mounts mounts;

  SAPI_ASSERT_OK_AND_ASSIGN(std::string path, CreateTempDir(file::JoinPath(
                                             GetTestTempPath(), "testdir_")));
  SAPI_ASSERT_OK_AND_ASSIGN(std::string symlink_path,
                       CreateNamedTempFileAndClose(
                           file::JoinPath(GetTestTempPath(), "testdir_")));

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
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(mounts.AddFileAt("/f", "/c"),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(mounts.AddDirectoryAt("/f", "/c"), IsOk());

  EXPECT_THAT(mounts.AddFile("/c/d/e"),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(mounts.AddFileAt("/f", "/c/d/e"),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(mounts.AddDirectoryAt("/f", "/c/d/e"),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(MountTreeTest, TestEvilNullByte) {
  Mounts mounts;
  // create the filename with a null byte this way as g4 fix forces newlines
  // otherwise.
  std::string filename = "/a/b";
  filename[2] = '\0';

  EXPECT_THAT(mounts.AddFile(filename),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddFileAt(filename, "/a"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddFileAt("/a", filename),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddDirectoryAt(filename, "/a"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddDirectoryAt("/a", filename),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(mounts.AddTmpfs(filename, kTmpfsSize),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(MountTreeTest, TestMinimalDynamicBinary) {
  Mounts mounts;
  EXPECT_THAT(mounts.AddMappingsForBinary(
                  GetTestSourcePath("sandbox2/testcases/minimal_dynamic")),
              IsOk());
  EXPECT_THAT(mounts.AddFile("/lib/x86_64-linux-gnu/libc.so.6"), IsOk());
}

TEST(MountTreeTest, TestList) {
  struct TestCase {
    const char *path;
    const bool is_ro;
  };
  // clang-format off
  const TestCase test_cases[] = {
      // NOTE: Directories have a trailing '/'; files don't.
      {"/a/b", true},
      {"/a/c/", true},
      {"/a/c/d/e/f/g", true},
      {"/h", true},
      {"/i/j/k", false},
      {"/i/l/", false},
  };
  // clang-format on

  Mounts mounts;

  // Create actual directories and files on disk and selectively add
  for (const auto &test_case : test_cases) {
    const auto inside_path = test_case.path;
    const std::string outside_path = absl::StrCat("/some/dir/", inside_path);
    if (absl::EndsWith(outside_path, "/")) {
      ASSERT_THAT(
          mounts.AddDirectoryAt(file::CleanPath(outside_path),
                                file::CleanPath(inside_path), test_case.is_ro),
          IsOk());
    } else {
      ASSERT_THAT(
          mounts.AddFileAt(file::CleanPath(outside_path),
                           file::CleanPath(inside_path), test_case.is_ro),
          IsOk());
    }
  }

  ASSERT_THAT(mounts.AddTmpfs(file::CleanPath("/d"), 1024 * 1024), IsOk());

  std::vector<std::string> outside_entries;
  std::vector<std::string> inside_entries;
  mounts.RecursivelyListMounts(&outside_entries, &inside_entries);

  // clang-format off
  EXPECT_THAT(
      inside_entries,
      UnorderedElementsAreArray({
          "R /a/b",
          "R /a/c/",
          "R /a/c/d/e/f/g",
          "R /h",
          "W /i/j/k",
          "W /i/l/",
          "/d",
      }));
  EXPECT_THAT(
      outside_entries,
      UnorderedElementsAreArray({
          absl::StrCat("/some/dir/", "a/b"),
          absl::StrCat("/some/dir/", "a/c/"),
          absl::StrCat("/some/dir/", "a/c/d/e/f/g"),
          absl::StrCat("/some/dir/", "h"),
          absl::StrCat("/some/dir/", "i/j/k"),
          absl::StrCat("/some/dir/", "i/l/"),
          absl::StrCat("tmpfs: size=", 1024*1024),
      }));
  // clang-format on
}

}  // namespace
}  // namespace sandbox2
