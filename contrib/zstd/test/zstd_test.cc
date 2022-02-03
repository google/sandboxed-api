// Copyright 2022 Google LLC
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

#include <fcntl.h>
#include <unistd.h>

#include <fstream>
#include <string>

#include "contrib/zstd/sandboxed.h"
#include "contrib/zstd/utils/utils_zstd.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sandboxed_api/util/temp_file.h"

namespace {

using ::sapi::IsOk;

std::string GetTestFilePath(const std::string& filename) {
  return sapi::file::JoinPath(getenv("TEST_FILES_DIR"), filename);
}

bool CompareFiles(const std::string& name1, const std::string& name2) {
  std::ifstream f1(name1, std::ios::binary);
  if (!f1.is_open()) {
    return false;
  }

  std::ifstream f2(name2, std::ios::binary);
  if (!f2.is_open()) {
    return false;
  }

  while (!f1.eof() && !f2.eof()) {
    char buf1[128];
    char buf2[128];

    f1.read(buf1, sizeof(buf1));
    f2.read(buf2, sizeof(buf2));

    if (f1.gcount() != f2.gcount()) {
      return false;
    }
    if (memcmp(&buf1, &buf2, f2.gcount()) != 0) {
      return false;
    }
  }

  return f1.eof() == f2.eof();
}

TEST(SandboxTest, CheckVersion) {
  ZstdSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZstdApi api = ZstdApi(&sandbox);

  absl::StatusOr<unsigned> version = api.ZSTD_versionNumber();
  ASSERT_THAT(version, IsOk())
      << "fatal error when invoking ZSTD_versionNumber";

  ASSERT_GE(*version, 10000);
}

TEST(SandboxTest, CheckMinCLevel) {
  ZstdSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZstdApi api = ZstdApi(&sandbox);

  absl::StatusOr<int> level = api.ZSTD_minCLevel();
  ASSERT_THAT(level, IsOk()) << "fatal error when invoking ZSTD_minCLevel";

  ASSERT_LT(*level, 0);
}

TEST(SandboxTest, CheckMaxCLevel) {
  ZstdSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZstdApi api = ZstdApi(&sandbox);

  absl::StatusOr<int> level = api.ZSTD_maxCLevel();
  ASSERT_THAT(level, IsOk()) << "fatal error when invoking ZSTD_maxCLevel";

  ASSERT_GT(*level, 0);
}

TEST(SandboxTest, CheckCompressInMemory) {
  ZstdSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZstdApi api = ZstdApi(&sandbox);

  std::string infile_s = GetTestFilePath("text");

  absl::StatusOr<std::string> path =
      sapi::CreateNamedTempFileAndClose("out.zstd");
  ASSERT_THAT(path, IsOk()) << "Could not create temp output file";
  std::string outfile_s =
      sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *path);

  std::ifstream infile(infile_s, std::ios::binary);
  ASSERT_TRUE(infile.is_open());

  std::ofstream outfile(outfile_s, std::ios::binary);
  ASSERT_TRUE(outfile.is_open());

  absl::Status status = CompressInMemory(api, infile, outfile, 0);
  ASSERT_THAT(status, IsOk()) << "Unable to compress file in memory";

  ASSERT_LT(outfile.tellp(), infile.tellg());
}

TEST(SandboxTest, CheckDecompressInMemory) {
  ZstdSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZstdApi api = ZstdApi(&sandbox);

  std::string infile_s = GetTestFilePath("text.blob.zstd");

  absl::StatusOr<std::string> path = sapi::CreateNamedTempFileAndClose("out");
  ASSERT_THAT(path, IsOk()) << "Could not create temp output file";
  std::string outfile_s =
      sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *path);

  std::ifstream infile(infile_s, std::ios::binary);
  ASSERT_TRUE(infile.is_open());

  std::ofstream outfile(outfile_s, std::ios::binary);
  ASSERT_TRUE(outfile.is_open());

  absl::Status status = DecompressInMemory(api, infile, outfile);
  ASSERT_THAT(status, IsOk()) << "Unable to decompress file in memory";

  ASSERT_GT(outfile.tellp(), infile.tellg());

  ASSERT_TRUE(CompareFiles(GetTestFilePath("text"), outfile_s));
}

TEST(SandboxTest, CheckCompressAndDecompressInMemory) {
  ZstdSapiSandbox sandbox;
  absl::Status status;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZstdApi api = ZstdApi(&sandbox);

  std::string infile_s = GetTestFilePath("text");

  absl::StatusOr<std::string> path_middle =
      sapi::CreateNamedTempFileAndClose("middle.zstd");
  ASSERT_THAT(path_middle, IsOk()) << "Could not create temp output file";
  std::string middle_s =
      sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *path_middle);

  absl::StatusOr<std::string> path = sapi::CreateNamedTempFileAndClose("out");
  ASSERT_THAT(path, IsOk()) << "Could not create temp output file";
  std::string outfile_s =
      sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *path);

  std::ifstream infile(infile_s, std::ios::binary);
  ASSERT_TRUE(infile.is_open());

  std::ofstream outmiddle(middle_s, std::ios::binary);
  ASSERT_TRUE(outmiddle.is_open());

  status = CompressInMemory(api, infile, outmiddle, 0);
  ASSERT_THAT(status, IsOk()) << "Unable to compress file in memory";

  ASSERT_LT(outmiddle.tellp(), infile.tellg());

  std::ifstream inmiddle(middle_s, std::ios::binary);
  ASSERT_TRUE(inmiddle.is_open());

  std::ofstream outfile(outfile_s, std::ios::binary);
  ASSERT_TRUE(outfile.is_open());

  status = DecompressInMemory(api, inmiddle, outfile);
  ASSERT_THAT(status, IsOk()) << "Unable to decompress file in memory";

  ASSERT_TRUE(CompareFiles(infile_s, outfile_s));
}

TEST(SandboxTest, CheckCompressStream) {
  ZstdSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZstdApi api = ZstdApi(&sandbox);

  std::string infile_s = GetTestFilePath("text");

  absl::StatusOr<std::string> path =
      sapi::CreateNamedTempFileAndClose("out.zstd");
  ASSERT_THAT(path, IsOk()) << "Could not create temp output file";
  std::string outfile_s =
      sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *path);

  std::ifstream infile(infile_s, std::ios::binary);
  ASSERT_TRUE(infile.is_open());

  std::ofstream outfile(outfile_s, std::ios::binary);
  ASSERT_TRUE(outfile.is_open());

  absl::Status status = CompressStream(api, infile, outfile, 0);
  ASSERT_THAT(status, IsOk()) << "Unable to compress stream";

  infile.clear();

  ASSERT_LT(outfile.tellp(), infile.tellg());
}

TEST(SandboxTest, CheckDecompressStream) {
  ZstdSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZstdApi api = ZstdApi(&sandbox);

  std::string infile_s = GetTestFilePath("text.stream.zstd");

  absl::StatusOr<std::string> path = sapi::CreateNamedTempFileAndClose("out");
  ASSERT_THAT(path, IsOk()) << "Could not create temp output file";
  std::string outfile_s =
      sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *path);

  std::ifstream infile(infile_s, std::ios::binary);
  ASSERT_TRUE(infile.is_open());

  std::ofstream outfile(outfile_s, std::ios::binary);
  ASSERT_TRUE(outfile.is_open());

  absl::Status status = DecompressStream(api, infile, outfile);
  ASSERT_THAT(status, IsOk()) << "Unable to decompress stream";

  ASSERT_GT(outfile.tellp(), infile.tellg());

  ASSERT_TRUE(CompareFiles(GetTestFilePath("text"), outfile_s));
}

TEST(SandboxTest, CheckCompressAndDecompressStream) {
  ZstdSapiSandbox sandbox;
  absl::Status status;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZstdApi api = ZstdApi(&sandbox);

  std::string infile_s = GetTestFilePath("text");

  absl::StatusOr<std::string> path_middle =
      sapi::CreateNamedTempFileAndClose("middle.zstd");
  ASSERT_THAT(path_middle, IsOk()) << "Could not create temp output file";
  std::string middle_s =
      sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *path_middle);

  absl::StatusOr<std::string> path = sapi::CreateNamedTempFileAndClose("out");
  ASSERT_THAT(path, IsOk()) << "Could not create temp output file";
  std::string outfile_s =
      sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *path);

  std::ifstream infile(infile_s, std::ios::binary);
  ASSERT_TRUE(infile.is_open());

  std::ofstream outmiddle(middle_s, std::ios::binary);
  ASSERT_TRUE(outmiddle.is_open());

  status = CompressStream(api, infile, outmiddle, 0);
  ASSERT_THAT(status, IsOk()) << "Unable to compress stream";

  infile.clear();
  ASSERT_LT(outmiddle.tellp(), infile.tellg());

  std::ifstream inmiddle(middle_s, std::ios::binary);
  ASSERT_TRUE(inmiddle.is_open());

  std::ofstream outfile(outfile_s, std::ios::binary);
  ASSERT_TRUE(outfile.is_open());

  status = DecompressStream(api, inmiddle, outfile);
  ASSERT_THAT(status, IsOk()) << "Unable to decompress";

  ASSERT_TRUE(CompareFiles(infile_s, outfile_s));
}

TEST(SandboxTest, CheckCompressInMemoryFD) {
  ZstdSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZstdApi api = ZstdApi(&sandbox);

  std::string infile_s = GetTestFilePath("text");

  absl::StatusOr<std::string> path =
      sapi::CreateNamedTempFileAndClose("out.zstd");
  ASSERT_THAT(path, IsOk()) << "Could not create temp output file";
  std::string outfile_s =
      sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *path);

  sapi::v::Fd infd(open(infile_s.c_str(), O_RDONLY));
  ASSERT_GE(infd.GetValue(), 0);
  sapi::v::Fd outfd(open(outfile_s.c_str(), O_WRONLY));
  ASSERT_GE(outfd.GetValue(), 0);

  absl::Status status = CompressInMemoryFD(api, infd, outfd, 0);
  ASSERT_THAT(status, IsOk()) << "Unable to compress file in memory";

  off_t inpos = lseek(infd.GetValue(), 0, SEEK_END);
  EXPECT_GE(inpos, 0);
  off_t outpos = lseek(outfd.GetValue(), 0, SEEK_END);
  EXPECT_GE(outpos, 0);

  EXPECT_LT(outpos, inpos);
}

TEST(SandboxTest, CheckDecompressInMemoryFD) {
  ZstdSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZstdApi api = ZstdApi(&sandbox);

  std::string infile_s = GetTestFilePath("text.blob.zstd");
  sapi::v::Fd infd(open(infile_s.c_str(), O_RDONLY));
  ASSERT_GE(infd.GetValue(), 0);

  absl::StatusOr<std::string> path = sapi::CreateNamedTempFileAndClose("out");
  ASSERT_THAT(path, IsOk()) << "Could not create temp output file";
  std::string outfile_s =
      sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *path);
  sapi::v::Fd outfd(open(outfile_s.c_str(), O_WRONLY));
  ASSERT_GE(outfd.GetValue(), 0);

  absl::Status status = DecompressInMemoryFD(api, infd, outfd);
  ASSERT_THAT(status, IsOk()) << "Unable to compress file in memory";

  off_t inpos = lseek(infd.GetValue(), 0, SEEK_END);
  EXPECT_GE(inpos, 0);

  off_t outpos = lseek(outfd.GetValue(), 0, SEEK_END);
  EXPECT_GE(outpos, 0);

  EXPECT_GT(outpos, inpos);

  ASSERT_TRUE(CompareFiles(GetTestFilePath("text"), outfile_s));
}

TEST(SandboxTest, CheckCompressAndDecompressInMemoryFD) {
  ZstdSapiSandbox sandbox;
  absl::Status status;
  int ret;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZstdApi api = ZstdApi(&sandbox);

  std::string infile_s = GetTestFilePath("text");

  absl::StatusOr<std::string> path_middle =
      sapi::CreateNamedTempFileAndClose("middle.zstd");
  ASSERT_THAT(path_middle, IsOk()) << "Could not create temp output file";
  std::string middle_s =
      sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *path_middle);

  absl::StatusOr<std::string> path = sapi::CreateNamedTempFileAndClose("out");
  ASSERT_THAT(path, IsOk()) << "Could not create temp output file";
  std::string outfile_s =
      sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *path);

  sapi::v::Fd infd(open(infile_s.c_str(), O_RDONLY));
  ASSERT_GE(infd.GetValue(), 0);

  sapi::v::Fd outmiddlefd(open(middle_s.c_str(), O_WRONLY));
  ASSERT_GE(outmiddlefd.GetValue(), 0);

  status = CompressInMemoryFD(api, infd, outmiddlefd, 0);
  ASSERT_THAT(status, IsOk()) << "Unable to compress file in memory";

  off_t inpos = lseek(infd.GetValue(), 0, SEEK_END);
  EXPECT_GE(inpos, 0);

  off_t outpos = lseek(outmiddlefd.GetValue(), 0, SEEK_END);
  EXPECT_GE(outpos, 0);

  EXPECT_LT(outpos, inpos);

  infd.CloseLocalFd();
  outmiddlefd.CloseLocalFd();

  sapi::v::Fd inmiddlefd(open(middle_s.c_str(), O_RDONLY));
  ASSERT_GE(inmiddlefd.GetValue(), 0);

  sapi::v::Fd outfd(open(outfile_s.c_str(), O_WRONLY));
  ASSERT_GE(outfd.GetValue(), 0);

  status = DecompressInMemoryFD(api, inmiddlefd, outfd);
  ASSERT_THAT(status, IsOk()) << "Unable to decompress file in memory";

  outfd.CloseLocalFd();
  inmiddlefd.CloseLocalFd();

  ASSERT_TRUE(CompareFiles(infile_s, outfile_s));
}

TEST(SandboxTest, CheckCompressStreamFD) {
  absl::Status status;
  ZstdSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZstdApi api = ZstdApi(&sandbox);

  std::string infile_s = GetTestFilePath("text");

  absl::StatusOr<std::string> path =
      sapi::CreateNamedTempFileAndClose("out.zstd");
  ASSERT_THAT(path, IsOk()) << "Could not create temp output file";
  std::string outfile_s =
      sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *path);

  sapi::v::Fd infd(open(infile_s.c_str(), O_RDONLY));
  ASSERT_GE(infd.GetValue(), 0);

  sapi::v::Fd outfd(open(outfile_s.c_str(), O_WRONLY));
  ASSERT_GE(outfd.GetValue(), 0);

  status = CompressStreamFD(api, infd, outfd, 0);
  ASSERT_THAT(status, IsOk()) << "Unable to compress stream";

  off_t inpos = lseek(infd.GetValue(), 0, SEEK_END);
  EXPECT_GE(inpos, 0);

  off_t outpos = lseek(outfd.GetValue(), 0, SEEK_END);
  EXPECT_GE(outpos, 0);

  EXPECT_LT(outpos, inpos);
}

TEST(SandboxTest, CheckDecompressStreamFD) {
  absl::Status status;
  ZstdSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZstdApi api = ZstdApi(&sandbox);

  std::string infile_s = GetTestFilePath("text.stream.zstd");

  absl::StatusOr<std::string> path = sapi::CreateNamedTempFileAndClose("out");
  ASSERT_THAT(path, IsOk()) << "Could not create temp output file";
  std::string outfile_s =
      sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *path);

  sapi::v::Fd infd(open(infile_s.c_str(), O_RDONLY));
  ASSERT_GE(infd.GetValue(), 0);

  sapi::v::Fd outfd(open(outfile_s.c_str(), O_WRONLY));
  ASSERT_GE(outfd.GetValue(), 0);

  status = DecompressStreamFD(api, infd, outfd);
  ASSERT_THAT(status, IsOk()) << "Unable to decompress stream";

  off_t inpos = lseek(infd.GetValue(), 0, SEEK_END);
  EXPECT_GE(inpos, 0);

  off_t outpos = lseek(outfd.GetValue(), 0, SEEK_END);
  EXPECT_GE(outpos, 0);

  EXPECT_GT(outpos, inpos);

  ASSERT_TRUE(CompareFiles(GetTestFilePath("text"), outfile_s));
}

TEST(SandboxTest, CheckCompressAndDecompressStreamFD) {
  ZstdSapiSandbox sandbox;
  absl::Status status;
  int ret;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZstdApi api = ZstdApi(&sandbox);

  std::string infile_s = GetTestFilePath("text");

  absl::StatusOr<std::string> path_middle =
      sapi::CreateNamedTempFileAndClose("middle.zstd");
  ASSERT_THAT(path_middle, IsOk()) << "Could not create temp output file";
  std::string middle_s =
      sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *path_middle);

  absl::StatusOr<std::string> path = sapi::CreateNamedTempFileAndClose("out");
  ASSERT_THAT(path, IsOk()) << "Could not create temp output file";
  std::string outfile_s =
      sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *path);

  sapi::v::Fd infd(open(infile_s.c_str(), O_RDONLY));
  ASSERT_GE(infd.GetValue(), 0);

  sapi::v::Fd outmiddlefd(open(middle_s.c_str(), O_WRONLY));
  ASSERT_GE(outmiddlefd.GetValue(), 0);

  status = CompressStreamFD(api, infd, outmiddlefd, 0);
  ASSERT_THAT(status, IsOk()) << "Unable to compress stream";

  off_t inpos = lseek(infd.GetValue(), 0, SEEK_END);
  EXPECT_GE(inpos, 0);

  off_t outmiddlepos = lseek(outmiddlefd.GetValue(), 0, SEEK_END);
  EXPECT_GE(outmiddlepos, 0);

  EXPECT_LT(outmiddlepos, inpos);

  infd.CloseLocalFd();
  outmiddlefd.CloseLocalFd();

  sapi::v::Fd inmiddlefd(open(middle_s.c_str(), O_RDONLY));
  ASSERT_GE(inmiddlefd.GetValue(), 0);

  sapi::v::Fd outfd(open(outfile_s.c_str(), O_WRONLY));
  ASSERT_GE(outfd.GetValue(), 0);

  status = DecompressStreamFD(api, inmiddlefd, outfd);
  ASSERT_THAT(status, IsOk()) << "Unable to decompress stream";

  ASSERT_TRUE(CompareFiles(infile_s, outfile_s));
}

}  // namespace
