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

#include <sched.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/cleanup/cleanup.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sandbox2::util {
namespace {

using ::sapi::GetTestSourcePath;
using ::sapi::IsOk;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::Ne;
using ::testing::Not;
using ::testing::StartsWith;
using ::testing::StrEq;

constexpr absl::string_view kTestString = "This is a test string";

TEST(UtilTest, TestCreateMemFd) {
  int fd = 0;
  ASSERT_THAT(CreateMemFd(&fd), IsTrue());
  EXPECT_THAT(fd, Gt(1));
  close(fd);
}

TEST(CharPtrArrayTest, FromStringVector) {
  std::vector<std::string> strings = {"a", "b", "c"};
  CharPtrArray array = CharPtrArray::FromStringVector(strings);
  EXPECT_THAT(array.ToStringVector(), Eq(strings));
  EXPECT_THAT(array.array(),
              ElementsAre(StrEq("a"), StrEq("b"), StrEq("c"), nullptr));
  EXPECT_THAT(array.data(), Eq(array.array().data()));
}

TEST(CharPtrArrayTest, FromCharPtrArray) {
  std::vector<std::string> strings = {"a", "b", "c"};
  std::vector<char*> string_arr;
  for (std::string& s : strings) {
    string_arr.push_back(s.data());
  }
  string_arr.push_back(nullptr);
  CharPtrArray array(string_arr.data());
  EXPECT_THAT(array.ToStringVector(), Eq(strings));
  EXPECT_THAT(array.array(),
              ElementsAre(StrEq("a"), StrEq("b"), StrEq("c"), nullptr));
  EXPECT_THAT(array.data(), Eq(array.array().data()));
}

TEST(GetProcStatusLineTest, Pid) {
  std::string line = GetProcStatusLine(getpid(), "Pid");
  EXPECT_THAT(line, Eq(absl::StrCat(getpid())));
}

TEST(GetProcStatusLineTest, NonExisting) {
  std::string line =
      GetProcStatusLine(getpid(), "__N_o_n_ExistingStatusSetting");
  EXPECT_THAT(line, IsEmpty());
}

TEST(ForkWithFlagsTest, DoesForkNormally) {
  int pfds[2];
  ASSERT_THAT(pipe(pfds), Eq(0));
  pid_t child = ForkWithFlags(SIGCHLD);
  ASSERT_THAT(child, Ne(-1));
  if (child == 0) {
    char c = 'a';
    if (!write(pfds[1], &c, 1)) {
      exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
  }
  close(pfds[1]);
  char c = ' ';
  EXPECT_THAT(read(pfds[0], &c, 1), Eq(1));
  close(pfds[0]);
  EXPECT_THAT(c, Eq('a'));
  int status;
  ASSERT_THAT(TEMP_FAILURE_RETRY(waitpid(child, &status, 0)), Eq(child));
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_THAT(WEXITSTATUS(status), Eq(0));
}

TEST(ForkWithFlagsTest, UnsupportedFlag) {
  EXPECT_THAT(ForkWithFlags(CLONE_CHILD_CLEARTID), Eq(-1));
}

TEST(ReadCPathFromPidSplitPageTest, Normal) {
  std::string test_str(kTestString);
  absl::StatusOr<std::string> read =
      ReadCPathFromPid(getpid(), reinterpret_cast<uintptr_t>(test_str.data()));
  ASSERT_THAT(read, IsOk());
  EXPECT_THAT(*read, Eq(kTestString));
}

TEST(ReadCPathFromPidSplitPageTest, Overlong) {
  std::string test_str(PATH_MAX + 1, 'a');
  absl::StatusOr<std::string> read =
      ReadCPathFromPid(getpid(), reinterpret_cast<uintptr_t>(test_str.data()));
  EXPECT_THAT(read, Not(IsOk()));
}

TEST(ReadCPathFromPidSplitPageTest, SplitPage) {
  const uintptr_t page_size = getpagesize();
  char* res = reinterpret_cast<char*>(mmap(nullptr, 2 * page_size,
                                           PROT_READ | PROT_WRITE,
                                           MAP_ANONYMOUS | MAP_PRIVATE, 0, 0));
  ASSERT_THAT(res, Ne(MAP_FAILED));
  absl::Cleanup cleanup = [res, page_size]() {
    ASSERT_THAT(munmap(res, 2 * page_size), Eq(0));
  };
  char* str = &res[page_size - kTestString.size() / 2];
  memcpy(str, kTestString.data(), kTestString.size());
  absl::StatusOr<std::string> read =
      ReadCPathFromPid(getpid(), reinterpret_cast<uintptr_t>(str));
  ASSERT_THAT(read, IsOk());
  EXPECT_THAT(*read, Eq(kTestString));
}

TEST(ReadCPathFromPidSplitPageTest, NearUnreadableMemory) {
  const uintptr_t page_size = getpagesize();
  char* res = reinterpret_cast<char*>(mmap(nullptr, 2 * page_size,
                                           PROT_READ | PROT_WRITE,
                                           MAP_ANONYMOUS | MAP_PRIVATE, 0, 0));
  ASSERT_THAT(res, Ne(MAP_FAILED));
  absl::Cleanup cleanup = [res, page_size]() {
    ASSERT_THAT(munmap(res, 2 * page_size), Eq(0));
  };
  ASSERT_THAT(mprotect(&res[page_size], page_size, PROT_NONE), Eq(0));
  char* str = &res[page_size - kTestString.size() - 1];
  memcpy(str, kTestString.data(), kTestString.size());
  absl::StatusOr<std::string> read =
      ReadCPathFromPid(getpid(), reinterpret_cast<uintptr_t>(str));
  ASSERT_THAT(read, IsOk());
  EXPECT_THAT(*read, Eq(kTestString));
}

TEST(CommunitacteTest, Normal) {
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/util_communicate");
  std::string output;
  SAPI_ASSERT_OK_AND_ASSIGN(
      int exit_code,
      util::Communicate({path, "argv1", "argv2"}, {"env1", "env2"}, &output));
  EXPECT_THAT(exit_code, Eq(0));
  EXPECT_THAT(output, StartsWith("3\nargv1\nargv2\nenv1\nenv2\n"));
}

}  // namespace
}  // namespace sandbox2::util
