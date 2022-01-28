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

#include "sandboxed_api/util/fileops.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/file_helpers.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sapi::file_util {

// Forward declare functions that are only used in fileops.cc.
namespace fileops {
bool GetCWD(std::string* result);
bool RemoveLastPathComponent(const std::string& file, std::string* output);
}  // namespace fileops

namespace {

using ::sapi::IsOk;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Ne;
using ::testing::SizeIs;
using ::testing::StrEq;

class FileOpsTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    ASSERT_THAT(chdir(GetTestTempPath().c_str()), Eq(0));
  }
};

TEST_F(FileOpsTest, GetCWDTest) {
  std::string result;
  ASSERT_THAT(fileops::GetCWD(&result), IsTrue());
  EXPECT_THAT(result, StrEq(GetTestTempPath()));
}

TEST_F(FileOpsTest, MakeAbsoluteTest) {
  const auto tmp_dir = GetTestTempPath();
  ASSERT_THAT(chdir(tmp_dir.c_str()), Eq(0));
  EXPECT_THAT(fileops::MakeAbsolute("", ""), StrEq(""));
  EXPECT_THAT(fileops::MakeAbsolute(".", ""), StrEq(tmp_dir));
  EXPECT_THAT(fileops::MakeAbsolute(".", tmp_dir), StrEq(tmp_dir));
  EXPECT_THAT(fileops::MakeAbsolute(".", "/"), StrEq("/"));
  EXPECT_THAT(fileops::MakeAbsolute("/", tmp_dir), StrEq("/"));
  EXPECT_THAT(fileops::MakeAbsolute("/", "/"), StrEq("/"));
  EXPECT_THAT(fileops::MakeAbsolute("/", ""), StrEq("/"));
  EXPECT_THAT(fileops::MakeAbsolute("/foo/bar", ""), StrEq("/foo/bar"));
  EXPECT_THAT(fileops::MakeAbsolute("foo/bar", ""),
              StrEq(absl::StrCat(tmp_dir, "/foo/bar")));
  EXPECT_THAT(fileops::MakeAbsolute("foo/bar", tmp_dir),
              StrEq(absl::StrCat(tmp_dir, "/foo/bar")));
  EXPECT_THAT(fileops::MakeAbsolute("foo/bar", tmp_dir + "/"),
              StrEq(absl::StrCat(tmp_dir, "/foo/bar")));
}

TEST_F(FileOpsTest, ExistsTest) {
  ASSERT_THAT(file::SetContents("exists_test", "", file::Defaults()), IsOk());
  EXPECT_THAT(fileops::Exists("exists_test", false), IsTrue());
  EXPECT_THAT(fileops::Exists("exists_test", true), IsTrue());

  ASSERT_THAT(symlink("exists_test", "exists_test_link"), Eq(0));
  EXPECT_THAT(fileops::Exists("exists_test_link", false), IsTrue());
  EXPECT_THAT(fileops::Exists("exists_test_link", true), IsTrue());

  ASSERT_THAT(unlink("exists_test"), Eq(0));
  EXPECT_THAT(fileops::Exists("exists_test_link", false), IsTrue());
  EXPECT_THAT(fileops::Exists("exists_test_link", true), IsFalse());

  ASSERT_THAT(unlink("exists_test_link"), Eq(0));
  EXPECT_THAT(fileops::Exists("exists_test_link", false), IsFalse());
  EXPECT_THAT(fileops::Exists("exists_test_link", true), IsFalse());
}

TEST_F(FileOpsTest, ReadLinkTest) {
  EXPECT_THAT(fileops::ReadLink("readlink_not_there"), StrEq(""));
  EXPECT_THAT(errno, Eq(ENOENT));

  ASSERT_THAT(file::SetContents("readlink_file", "", file::Defaults()), IsOk());
  EXPECT_THAT(fileops::ReadLink("readlink_file"), StrEq(""));
  unlink("readlink_file");

  ASSERT_THAT(symlink("..", "readlink_dotdot"), Eq(0));
  EXPECT_THAT(fileops::ReadLink("readlink_dotdot"), StrEq(".."));
  unlink("readlink_dotdot");

  ASSERT_THAT(symlink("../", "readlink_dotdotslash"), 0);
  EXPECT_THAT(fileops::ReadLink("readlink_dotdotslash"), "../");
  unlink("readlink_dotdotslash");

  ASSERT_THAT(symlink("/", "readlink_slash"), 0);
  EXPECT_THAT(fileops::ReadLink("readlink_slash"), "/");
  unlink("readlink_slash");

  const std::string very_long_name(PATH_MAX - 1, 'f');
  ASSERT_THAT(symlink(very_long_name.c_str(), "readlink_long"), Eq(0));
  EXPECT_THAT(fileops::ReadLink("readlink_long"), StrEq(very_long_name));
  unlink("readlink_long");
}

TEST_F(FileOpsTest, ListDirectoryEntriesFailTest) {
  std::vector<std::string> files;
  std::string error;

  EXPECT_THAT(fileops::ListDirectoryEntries("new_dir", &files, &error),
              IsFalse());
  EXPECT_THAT(files, IsEmpty());
  EXPECT_THAT(error, StrEq("opendir(new_dir): No such file or directory"));
}

TEST_F(FileOpsTest, ListDirectoryEntriesEmptyTest) {
  std::vector<std::string> files;
  std::string error;

  ASSERT_THAT(mkdir("new_dir", 0700), Eq(0));

  EXPECT_THAT(fileops::ListDirectoryEntries("new_dir", &files, &error),
              IsTrue());
  EXPECT_THAT(files, IsEmpty());

  rmdir("new_dir");
}

TEST_F(FileOpsTest, ListDirectoryEntriesOneFileTest) {
  ASSERT_THAT(mkdir("new_dir", 0700), Eq(0));
  ASSERT_THAT(file::SetContents("new_dir/first", "", file::Defaults()), IsOk());

  std::vector<std::string> files;
  std::string error;
  EXPECT_THAT(fileops::ListDirectoryEntries("new_dir", &files, &error),
              IsTrue());

  unlink("new_dir/first");
  rmdir("new_dir");

  ASSERT_THAT(files, SizeIs(1));
  EXPECT_THAT(files[0], "first");
}

TEST_F(FileOpsTest, ListDirectoryEntriesTest) {
  ASSERT_THAT(mkdir("new_dir", 0700), Eq(0));
  constexpr int kNumFiles = 10;
  for (int i = 0; i < kNumFiles; ++i) {
    ASSERT_THAT(file::SetContents(absl::StrCat("new_dir/file", i), "",
                                  file::Defaults()),
                IsOk());
  }

  std::vector<std::string> files;
  std::string error;
  EXPECT_THAT(fileops::ListDirectoryEntries("new_dir", &files, &error),
              IsTrue());

  fileops::DeleteRecursively("new_dir");

  ASSERT_THAT(files, SizeIs(kNumFiles));
  std::sort(files.begin(), files.end());
  for (int i = 0; i < kNumFiles; ++i) {
    EXPECT_THAT(files[i], StrEq(absl::StrCat("file", i)));
  }
}

TEST_F(FileOpsTest, RemoveLastPathComponentTest) {
  std::string result;

  EXPECT_THAT(fileops::RemoveLastPathComponent("/", &result), IsFalse());
  EXPECT_THAT(result, StrEq("/"));

  EXPECT_THAT(fileops::RemoveLastPathComponent("///", &result), IsFalse());
  EXPECT_THAT(result, StrEq("/"));

  EXPECT_THAT(fileops::RemoveLastPathComponent("/home", &result), IsTrue());
  EXPECT_THAT(result, StrEq("/"));

  EXPECT_THAT(fileops::RemoveLastPathComponent("/home/", &result), IsTrue());
  EXPECT_THAT(result, StrEq("/"));

  EXPECT_THAT(fileops::RemoveLastPathComponent("/home///", &result), IsTrue());
  EXPECT_THAT(result, StrEq("/"));

  EXPECT_THAT(fileops::RemoveLastPathComponent("/home///", &result), IsTrue());
  EXPECT_THAT(result, StrEq("/"));

  EXPECT_THAT(fileops::RemoveLastPathComponent("///home///", &result),
              IsTrue());
  EXPECT_THAT(result, StrEq("/"));

  EXPECT_THAT(fileops::RemoveLastPathComponent("/home/someone", &result),
              IsTrue());
  EXPECT_THAT(result, StrEq("/home"));

  EXPECT_THAT(fileops::RemoveLastPathComponent("/home///someone", &result),
              IsTrue());
  EXPECT_THAT(result, StrEq("/home"));

  EXPECT_THAT(fileops::RemoveLastPathComponent("/home///someone/", &result),
              IsTrue());
  EXPECT_THAT(result, StrEq("/home"));

  EXPECT_THAT(fileops::RemoveLastPathComponent("/home///someone//", &result),
              IsTrue());
  EXPECT_THAT(result, StrEq("/home"));

  EXPECT_THAT(fileops::RemoveLastPathComponent("/home/someone/file", &result),
              IsTrue());
  EXPECT_THAT(result, StrEq("/home/someone"));

  EXPECT_THAT(
      fileops::RemoveLastPathComponent("/home/someone////file", &result),
      IsTrue());
  EXPECT_THAT(result, StrEq("/home/someone"));

  EXPECT_THAT(fileops::RemoveLastPathComponent("/home///someone/file", &result),
              IsTrue());
  EXPECT_THAT(result, StrEq("/home///someone"));

  EXPECT_THAT(fileops::RemoveLastPathComponent("/home/someone/file", &result),
              IsTrue());
  EXPECT_THAT(result, StrEq("/home/someone"));

  EXPECT_THAT(fileops::RemoveLastPathComponent("no_root", &result), IsTrue());
  EXPECT_THAT(result, StrEq(""));

  EXPECT_THAT(fileops::RemoveLastPathComponent("no_root/", &result), IsTrue());
  EXPECT_THAT(result, StrEq(""));

  EXPECT_THAT(fileops::RemoveLastPathComponent("no_root///", &result),
              IsTrue());
  EXPECT_THAT(result, StrEq(""));

  EXPECT_THAT(fileops::RemoveLastPathComponent("/file", &result), IsTrue());
  EXPECT_THAT(result, "/");

  EXPECT_THAT(fileops::RemoveLastPathComponent("no_root/file", &result),
              IsTrue());
  EXPECT_THAT(result, StrEq("no_root"));

  result = "no_root";
  EXPECT_THAT(fileops::RemoveLastPathComponent(result, &result), IsTrue());
  EXPECT_THAT(result, StrEq(""));

  result = "no_root/";
  EXPECT_THAT(fileops::RemoveLastPathComponent(result, &result), IsTrue());
  EXPECT_THAT(result, StrEq(""));

  result = "no_root///";
  EXPECT_THAT(fileops::RemoveLastPathComponent(result, &result), IsTrue());
  EXPECT_THAT(result, StrEq(""));

  result = "/file";
  EXPECT_THAT(fileops::RemoveLastPathComponent(result, &result), IsTrue());
  EXPECT_THAT(result, StrEq("/"));

  result = "no_root/file";
  EXPECT_THAT(fileops::RemoveLastPathComponent(result, &result), IsTrue());
  EXPECT_THAT(result, StrEq("no_root"));

  EXPECT_THAT(fileops::RemoveLastPathComponent("", &result), IsFalse());
  EXPECT_THAT(result, StrEq(""));
}

TEST_F(FileOpsTest, TestBasename) {
  EXPECT_THAT(fileops::Basename(""), StrEq(""));
  EXPECT_THAT(fileops::Basename("/"), StrEq(""));
  EXPECT_THAT(fileops::Basename("//"), StrEq(""));
  EXPECT_THAT(fileops::Basename("/hello/"), StrEq(""));
  EXPECT_THAT(fileops::Basename("//hello"), StrEq("hello"));
  EXPECT_THAT(fileops::Basename("/hello/world"), StrEq("world"));
  EXPECT_THAT(fileops::Basename("/hello, world"), StrEq("hello, world"));
}

TEST_F(FileOpsTest, TestStripBasename) {
  EXPECT_THAT(fileops::StripBasename(""), StrEq(""));
  EXPECT_THAT(fileops::StripBasename("/"), StrEq("/"));
  EXPECT_THAT(fileops::StripBasename("//"), StrEq("/"));
  EXPECT_THAT(fileops::StripBasename("/hello"), StrEq("/"));
  EXPECT_THAT(fileops::StripBasename("//hello"), StrEq("/"));
  EXPECT_THAT(fileops::StripBasename("/hello/"), StrEq("/hello"));
  EXPECT_THAT(fileops::StripBasename("/hello//"), StrEq("/hello/"));
  EXPECT_THAT(fileops::StripBasename("/hello/world"), StrEq("/hello"));
  EXPECT_THAT(fileops::StripBasename("/hello, world"), StrEq("/"));
}

void SetupDirectory() {
  ASSERT_THAT(mkdir("foo", 0755), Eq(0));
  ASSERT_THAT(mkdir("foo/bar", 0755), Eq(0));
  ASSERT_THAT(mkdir("foo/baz", 0755), Eq(0));
  ASSERT_THAT(file::SetContents("foo/quux", "", file::Defaults()), IsOk());
  ASSERT_THAT(chmod("foo/quux", 0644), Eq(0));

  ASSERT_THAT(file::SetContents("foo/bar/foo", "", file::Defaults()), IsOk());
  ASSERT_THAT(chmod("foo/bar/foo", 0644), Eq(0));
  ASSERT_THAT(file::SetContents("foo/bar/bar", "", file::Defaults()), IsOk());
  ASSERT_THAT(chmod("foo/bar/bar", 0644), Eq(0));

  ASSERT_THAT(mkdir("foo/bar/baz", 0755), Eq(0));
  ASSERT_THAT(file::SetContents("foo/bar/baz/foo", "", file::Defaults()),
              IsOk());
  ASSERT_THAT(chmod("foo/bar/baz/foo", 0644), Eq(0));
}

TEST_F(FileOpsTest, DeleteRecursivelyTest) {
  EXPECT_THAT(fileops::DeleteRecursively("foo"), IsTrue());
  EXPECT_THAT(fileops::DeleteRecursively("/not_there"), IsTrue());

  // Can't stat file
  SetupDirectory();
  ASSERT_THAT(chmod("foo/bar/baz", 0000), Eq(0));
  EXPECT_THAT(fileops::DeleteRecursively("foo/bar/baz/quux"), IsFalse());
  EXPECT_THAT(errno, Eq(EACCES));
  ASSERT_THAT(chmod("foo/bar/baz", 0755), Eq(0));

  EXPECT_THAT(fileops::DeleteRecursively("foo"), IsTrue());
  struct stat64 st;
  EXPECT_THAT(lstat64("foo", &st), Ne(0));

  // Can't list subdirectory
  SetupDirectory();
  ASSERT_THAT(chmod("foo/bar/baz", 0000), Eq(0));
  EXPECT_THAT(fileops::DeleteRecursively("foo"), IsFalse());
  EXPECT_THAT(errno, Eq(EACCES));
  ASSERT_THAT(chmod("foo/bar/baz", 0755), Eq(0));

  EXPECT_THAT(fileops::DeleteRecursively("foo"), IsTrue());

  // Can't delete file
  SetupDirectory();
  ASSERT_THAT(chmod("foo/bar/baz", 0500), Eq(0));
  EXPECT_THAT(fileops::DeleteRecursively("foo"), IsFalse());
  EXPECT_THAT(errno, Eq(EACCES));
  ASSERT_THAT(chmod("foo/bar/baz", 0755), Eq(0));

  EXPECT_THAT(fileops::DeleteRecursively("foo"), IsTrue());

  // Can't delete directory
  SetupDirectory();
  ASSERT_THAT(fileops::DeleteRecursively("foo/bar/baz/foo"), IsTrue());
  ASSERT_THAT(chmod("foo/bar", 0500), Eq(0));
  EXPECT_THAT(fileops::DeleteRecursively("foo/bar/baz"), IsFalse());
  EXPECT_THAT(errno, Eq(EACCES));
  ASSERT_THAT(chmod("foo/bar", 0755), Eq(0));

  EXPECT_THAT(fileops::DeleteRecursively("foo"), IsTrue());
}

TEST_F(FileOpsTest, ReadLinkAbsoluteTest) {
  const auto tmp_dir = GetTestTempPath();
  ASSERT_THAT(chdir(tmp_dir.c_str()), Eq(0));

  EXPECT_THAT(fileops::DeleteRecursively("foo"), IsTrue());
  ASSERT_THAT(symlink("rel/path", "foo"), Eq(0));

  const std::string expected_path = absl::StrCat(tmp_dir, "/rel/path");
  const std::string expected_path2 = absl::StrCat(tmp_dir, "/./rel/path");
  std::string result;
  EXPECT_THAT(fileops::ReadLinkAbsolute("foo", &result), IsTrue());
  EXPECT_THAT(result, StrEq(expected_path));
  EXPECT_THAT(fileops::ReadLinkAbsolute("./foo", &result), IsTrue());
  EXPECT_THAT(result, StrEq(expected_path2));
  EXPECT_THAT(fileops::ReadLinkAbsolute(absl::StrCat(tmp_dir, "/foo"), &result),
              IsTrue());
  EXPECT_THAT(result, StrEq(expected_path));

  result.clear();
  EXPECT_THAT(fileops::ReadLinkAbsolute("/not_there", &result), IsFalse());
  EXPECT_THAT(result, IsEmpty());
}

TEST_F(FileOpsTest, CopyFileTest) {
  const auto tmp_dir = GetTestTempPath();
  // Non-existent source
  EXPECT_THAT(
      fileops::CopyFile("/not/there", absl::StrCat(tmp_dir, "/out"), 0777),
      IsFalse());

  // Unwritable target
  EXPECT_THAT(fileops::CopyFile("/proc/self/exe", tmp_dir, 0777), IsFalse());

  EXPECT_THAT(file::SetContents(absl::StrCat(tmp_dir, "/test"), "test\n",
                                file::Defaults()),
              IsOk());
  EXPECT_THAT(fileops::CopyFile(absl::StrCat(tmp_dir, "/test"),
                                absl::StrCat(tmp_dir, "/test2"), 0666),
              IsTrue());

  std::string text;
  EXPECT_THAT(file::GetContents(absl::StrCat(tmp_dir, "/test2"), &text,
                                file::Defaults()),
              IsOk());

  EXPECT_THAT(text, StrEq("test\n"));

  unlink((absl::StrCat(tmp_dir, "/test")).c_str());
  unlink((absl::StrCat(tmp_dir, "/test2")).c_str());
}

}  // namespace
}  // namespace sapi::file_util
