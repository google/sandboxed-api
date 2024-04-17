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
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sandbox2::util {
namespace {

using ::sapi::GetTestSourcePath;
using ::sapi::IsOk;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
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

TEST(ReadBytesFromPidTest, Normal) {
  absl::StatusOr<std::vector<uint8_t>> read = ReadBytesFromPid(
      getpid(), reinterpret_cast<uintptr_t>(kTestString.data()),
      kTestString.size());
  EXPECT_THAT(*read, ElementsAreArray(kTestString));
}

TEST(ReadBytesFromPidTest, NearUnmappedMemory) {
  const uintptr_t page_size = getpagesize();
  ASSERT_LE(kTestString.size(), page_size);
  char* res = reinterpret_cast<char*>(mmap(nullptr, 2 * page_size,
                                           PROT_READ | PROT_WRITE,
                                           MAP_ANONYMOUS | MAP_PRIVATE, 0, 0));
  ASSERT_THAT(res, Ne(MAP_FAILED));
  ASSERT_THAT(munmap(&res[page_size], page_size), Eq(0));
  absl::Cleanup cleanup = [res, page_size]() {
    ASSERT_THAT(munmap(res, page_size), Eq(0));
  };
  char* data = &res[page_size - kTestString.size()];
  memcpy(data, kTestString.data(), kTestString.size());
  absl::StatusOr<std::vector<uint8_t>> read = ReadBytesFromPid(
      getpid(), reinterpret_cast<uintptr_t>(data), 2 * kTestString.size());
  ASSERT_THAT(read, IsOk());
  EXPECT_THAT(*read, ElementsAreArray(kTestString));
}

class ReadBytesFromPidIntoTest : public testing::TestWithParam<bool> {
 protected:
  absl::StatusOr<size_t> Read(pid_t pid, uintptr_t ptr, absl::Span<char> data) {
    if (GetParam()) {
      return internal::ReadBytesFromPidWithReadv(pid, ptr, data);
    } else {
      return internal::ReadBytesFromPidWithReadvInSplitChunks(pid, ptr, data);
    }
  }
};

TEST_P(ReadBytesFromPidIntoTest, Normal) {
  char data[kTestString.size()] = {0};
  absl::StatusOr<size_t> bytes_read =
      Read(getpid(), reinterpret_cast<uintptr_t>(kTestString.data()),
           absl::MakeSpan(data));
  ASSERT_THAT(bytes_read, IsOk());
  EXPECT_THAT(*bytes_read, Eq(kTestString.size()));
  EXPECT_THAT(data, ElementsAreArray(kTestString));
}

TEST_P(ReadBytesFromPidIntoTest, SplitPage) {
  const uintptr_t page_size = getpagesize();
  ASSERT_LE(kTestString.size(), page_size);
  char* res = reinterpret_cast<char*>(mmap(nullptr, 2 * page_size,
                                           PROT_READ | PROT_WRITE,
                                           MAP_ANONYMOUS | MAP_PRIVATE, 0, 0));
  ASSERT_THAT(res, Ne(MAP_FAILED));
  absl::Cleanup cleanup = [res, page_size]() {
    ASSERT_THAT(munmap(res, 2 * page_size), Eq(0));
  };
  char* data = &res[page_size - kTestString.size() / 2];
  memcpy(data, kTestString.data(), kTestString.size());
  char output[kTestString.size()];
  absl::StatusOr<size_t> bytes_read =
      Read(getpid(), reinterpret_cast<uintptr_t>(data), absl::MakeSpan(output));
  ASSERT_THAT(bytes_read, IsOk());
  EXPECT_THAT(*bytes_read, Eq(kTestString.size()));
  EXPECT_THAT(output, ElementsAreArray(kTestString));
}

TEST_P(ReadBytesFromPidIntoTest, InvalidPid) {
  char data;
  absl::StatusOr<size_t> bytes_read =
      Read(-1, reinterpret_cast<uintptr_t>(&data), absl::MakeSpan(&data, 1));
  ASSERT_THAT(bytes_read, Not(IsOk()));
}

TEST_P(ReadBytesFromPidIntoTest, ZeroLength) {
  char data;
  absl::StatusOr<size_t> bytes_read = Read(
      getpid(), reinterpret_cast<uintptr_t>(&data), absl::MakeSpan(&data, 0));
  ASSERT_THAT(bytes_read, IsOk());
  ASSERT_THAT(*bytes_read, Eq(0));
}

TEST_P(ReadBytesFromPidIntoTest, ZeroLengthWithInvalidPid) {
  char data;
  absl::StatusOr<size_t> bytes_read =
      Read(-1, reinterpret_cast<uintptr_t>(&data), absl::MakeSpan(&data, 0));
  ASSERT_THAT(bytes_read, IsOk());
  ASSERT_THAT(*bytes_read, Eq(0));
}

TEST_P(ReadBytesFromPidIntoTest, UnmappedMemory) {
  const uintptr_t page_size = getpagesize();
  char* res =
      reinterpret_cast<char*>(mmap(nullptr, page_size, PROT_READ | PROT_WRITE,
                                   MAP_ANONYMOUS | MAP_PRIVATE, 0, 0));
  ASSERT_THAT(res, Ne(MAP_FAILED));
  ASSERT_THAT(munmap(res, page_size), Eq(0));
  absl::StatusOr<size_t> bytes_read =
      Read(getpid(), reinterpret_cast<uintptr_t>(res), absl::MakeSpan(res, 1));
  ASSERT_THAT(bytes_read, Not(IsOk()));
}

TEST_P(ReadBytesFromPidIntoTest, NearUnmappedMemory) {
  const uintptr_t page_size = getpagesize();
  char* res = reinterpret_cast<char*>(mmap(nullptr, 2 * page_size,
                                           PROT_READ | PROT_WRITE,
                                           MAP_ANONYMOUS | MAP_PRIVATE, 0, 0));
  ASSERT_THAT(res, Ne(MAP_FAILED));
  // Unmap second page so there's a gap.
  ASSERT_THAT(munmap(&res[page_size], page_size), Eq(0));
  absl::Cleanup cleanup = [res, page_size]() {
    ASSERT_THAT(munmap(res, page_size), Eq(0));
  };
  char* data = &res[page_size - kTestString.size() / 2];
  memcpy(data, kTestString.data(), kTestString.size() / 2);
  char output[kTestString.size()];
  absl::StatusOr<size_t> bytes_read =
      Read(getpid(), reinterpret_cast<uintptr_t>(data), absl::MakeSpan(output));
  ASSERT_THAT(bytes_read, IsOk());
  EXPECT_THAT(*bytes_read, Eq(kTestString.size() / 2));
  EXPECT_THAT(absl::MakeSpan(data, kTestString.size() / 2),
              Eq(absl::MakeSpan(kTestString.data(), kTestString.size() / 2)));
}

TEST_P(ReadBytesFromPidIntoTest, ExceedIovMax) {
  // Read one page past the max readable memory in one set of iovecs.
  const uintptr_t page_size = getpagesize();
  const size_t length = (IOV_MAX + 1) * page_size;
  char* data = reinterpret_cast<char*>(mmap(nullptr, length + page_size,
                                            PROT_READ | PROT_WRITE,
                                            MAP_ANONYMOUS | MAP_PRIVATE, 0, 0));
  ASSERT_THAT(data, Ne(MAP_FAILED));
  // Unmap second page so there's a gap.
  ASSERT_THAT(munmap(&data[length], page_size), Eq(0));
  absl::Cleanup cleanup = [data, length]() {
    ASSERT_THAT(munmap(data, length), Eq(0));
  };
  memset(data, 0x0e, length);
  std::vector<char> output(length);
  absl::StatusOr<size_t> bytes_read =
      Read(getpid(), reinterpret_cast<uintptr_t>(data),
           absl::MakeSpan(output.data(), length));
  ASSERT_THAT(bytes_read, IsOk());
  EXPECT_THAT(*bytes_read, Eq(length));
  EXPECT_THAT(output, ElementsAreArray(data, length));
}

INSTANTIATE_TEST_SUITE_P(ReadBytesFromPidInto, ReadBytesFromPidIntoTest,
                         testing::Values(true, false));

class WriteBytesToPidFromTest : public testing::TestWithParam<bool> {
 protected:
  absl::StatusOr<size_t> Write(pid_t pid, uintptr_t ptr,
                               absl::Span<const char> data) {
    if (GetParam()) {
      return internal::WriteBytesToPidWithWritev(pid, ptr, data);
    } else {
      return internal::WriteBytesToPidWithProcMem(pid, ptr, data);
    }
  };
};

TEST_P(WriteBytesToPidFromTest, Normal) {
  char data[kTestString.size()] = {0};
  absl::StatusOr<size_t> bytes_written =
      Write(getpid(), reinterpret_cast<uintptr_t>(data), kTestString);
  ASSERT_THAT(bytes_written, IsOk());
  EXPECT_THAT(*bytes_written, Eq(kTestString.size()));
  EXPECT_THAT(data, ElementsAreArray(kTestString));
}

TEST_P(WriteBytesToPidFromTest, SplitPage) {
  const uintptr_t page_size = getpagesize();
  ASSERT_LE(kTestString.size(), page_size);
  char* res = reinterpret_cast<char*>(mmap(nullptr, 2 * page_size,
                                           PROT_READ | PROT_WRITE,
                                           MAP_ANONYMOUS | MAP_PRIVATE, 0, 0));
  ASSERT_THAT(res, Ne(MAP_FAILED));
  absl::Cleanup cleanup = [res, page_size]() {
    ASSERT_THAT(munmap(res, 2 * page_size), Eq(0));
  };
  char* data = &res[page_size - kTestString.size() / 2];
  absl::StatusOr<size_t> bytes_written =
      Write(getpid(), reinterpret_cast<uintptr_t>(data), kTestString);
  ASSERT_THAT(bytes_written, IsOk());
  EXPECT_THAT(*bytes_written, Eq(kTestString.size()));
  EXPECT_THAT(kTestString, ElementsAreArray(data, kTestString.size()));
}

TEST_P(WriteBytesToPidFromTest, InvalidPid) {
  char data = 0;
  absl::StatusOr<size_t> bytes_written =
      Write(-1, reinterpret_cast<uintptr_t>(&data), absl::MakeSpan(&data, 1));
  ASSERT_THAT(bytes_written, Not(IsOk()));
}

TEST_P(WriteBytesToPidFromTest, ZeroLength) {
  char data = 0;
  absl::StatusOr<size_t> bytes_written = Write(
      getpid(), reinterpret_cast<uintptr_t>(&data), absl::MakeSpan(&data, 0));
  ASSERT_THAT(bytes_written, IsOk());
  ASSERT_THAT(*bytes_written, Eq(0));
}

TEST_P(WriteBytesToPidFromTest, ZeroLengthWithInvalidPid) {
  char data = 0;
  absl::StatusOr<size_t> bytes_written =
      Write(-1, reinterpret_cast<uintptr_t>(&data), absl::MakeSpan(&data, 0));
  ASSERT_THAT(bytes_written, IsOk());
  ASSERT_THAT(*bytes_written, Eq(0));
}

TEST_P(WriteBytesToPidFromTest, NearUnmappedMemory) {
  const uintptr_t page_size = getpagesize();
  char* res = reinterpret_cast<char*>(mmap(nullptr, 2 * page_size,
                                           PROT_READ | PROT_WRITE,
                                           MAP_ANONYMOUS | MAP_PRIVATE, 0, 0));
  ASSERT_THAT(res, Ne(MAP_FAILED));
  ASSERT_THAT(munmap(&res[page_size], page_size), Eq(0));
  absl::Cleanup cleanup = [res, page_size]() {
    ASSERT_THAT(munmap(res, page_size), Eq(0));
  };
  char* data = &res[page_size - kTestString.size() / 2];
  absl::StatusOr<size_t> bytes_written =
      Write(getpid(), reinterpret_cast<uintptr_t>(data), kTestString);
  ASSERT_THAT(bytes_written, IsOk());
  EXPECT_THAT(*bytes_written, Eq(kTestString.size() / 2));
  EXPECT_THAT(absl::MakeSpan(data, kTestString.size() / 2),
              Eq(absl::MakeSpan(kTestString.data(), kTestString.size() / 2)));
}

TEST_P(WriteBytesToPidFromTest, ExceedIovMax) {
  const uintptr_t page_size = getpagesize();
  const size_t length = (IOV_MAX + 1) * page_size;
  char* data = reinterpret_cast<char*>(mmap(nullptr, length + page_size,
                                            PROT_READ | PROT_WRITE,
                                            MAP_ANONYMOUS | MAP_PRIVATE, 0, 0));
  ASSERT_THAT(data, Ne(MAP_FAILED));
  // Unmap second page so there's a gap.
  ASSERT_THAT(munmap(&data[length], page_size), Eq(0));
  absl::Cleanup cleanup = [data, length]() {
    ASSERT_THAT(munmap(data, length), Eq(0));
  };
  memset(data, 0, length);
  const std::vector<char> src(length, 0x0e);
  absl::StatusOr<size_t> bytes_written =
      Write(getpid(), reinterpret_cast<uintptr_t>(data),
            absl::MakeSpan(src.data(), length));
  ASSERT_THAT(bytes_written, IsOk());
  EXPECT_THAT(*bytes_written, Eq(length));
  EXPECT_THAT(src, ElementsAreArray(data, length));
}

INSTANTIATE_TEST_SUITE_P(WriteBytesToPidFrom, WriteBytesToPidFromTest,
                         testing::Values(true, false));

}  // namespace
}  // namespace sandbox2::util
