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

#include <fstream>

#include "contrib/zopfli/sandboxed.h"
#include "contrib/zopfli/utils/utils_zopfli.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sandboxed_api/util/temp_file.h"

namespace {

using ::sapi::IsOk;
using ::testing::IsEmpty;
using ::testing::Not;

std::string GetTestFilePath(const std::string& filename) {
  return sapi::file::JoinPath(getenv("TEST_FILES_DIR"), filename);
}

std::string GetTemporaryFile(const std::string& filename) {
  absl::StatusOr<std::string> tmp_file =
      sapi::CreateNamedTempFileAndClose(filename);
  if (!tmp_file.ok()) {
    return "";
  }

  return sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *tmp_file);
}

class TestText : public testing::TestWithParam<ZopfliFormat> {};
class TestBinary : public testing::TestWithParam<ZopfliFormat> {};
class TestTextFD : public testing::TestWithParam<ZopfliFormat> {};
class TestBinaryFD : public testing::TestWithParam<ZopfliFormat> {};

TEST_P(TestText, Compress) {
  ZopfliSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZopfliApi api(&sandbox);

  std::string infile_s = GetTestFilePath("text");
  std::string outfile_s = GetTemporaryFile("text.out");
  ASSERT_THAT(outfile_s, Not(IsEmpty()));

  std::ifstream infile(infile_s, std::ios::binary);
  ASSERT_TRUE(infile.is_open());

  std::ofstream outfile(outfile_s, std::ios::binary);
  ASSERT_TRUE(outfile.is_open());

  absl::Status status = Compress(api, infile, outfile, GetParam());
  ASSERT_THAT(status, IsOk()) << "Unable to compress file";

  ASSERT_LT(outfile.tellp(), infile.tellg());
}

INSTANTIATE_TEST_SUITE_P(SandboxTest, TestText,
                         testing::Values(ZOPFLI_FORMAT_DEFLATE,
                                         ZOPFLI_FORMAT_GZIP,
                                         ZOPFLI_FORMAT_ZLIB));

TEST_P(TestBinary, Compress) {
  ZopfliSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZopfliApi api(&sandbox);

  std::string infile_s = GetTestFilePath("binary");
  std::string outfile_s = GetTemporaryFile("binary.out");
  ASSERT_THAT(outfile_s, Not(IsEmpty()));

  std::ifstream infile(infile_s, std::ios::binary);
  ASSERT_TRUE(infile.is_open());

  std::ofstream outfile(outfile_s, std::ios::binary);
  ASSERT_TRUE(outfile.is_open());

  absl::Status status = Compress(api, infile, outfile, GetParam());
  ASSERT_THAT(status, IsOk()) << "Unable to compress file";

  ASSERT_LT(outfile.tellp(), infile.tellg());
}

INSTANTIATE_TEST_SUITE_P(SandboxTest, TestBinary,
                         testing::Values(ZOPFLI_FORMAT_DEFLATE,
                                         ZOPFLI_FORMAT_GZIP,
                                         ZOPFLI_FORMAT_ZLIB));

TEST_P(TestTextFD, Compress) {
  ZopfliSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZopfliApi api = ZopfliApi(&sandbox);

  std::string infile_s = GetTestFilePath("text");
  std::string outfile_s = GetTemporaryFile("text.out");
  ASSERT_THAT(outfile_s, Not(IsEmpty()));

  sapi::v::Fd infd(open(infile_s.c_str(), O_RDONLY));
  ASSERT_GE(infd.GetValue(), 0);

  sapi::v::Fd outfd(open(outfile_s.c_str(), O_WRONLY));
  ASSERT_GE(outfd.GetValue(), 0);

  absl::Status status = CompressFD(api, infd, outfd, GetParam());
  ASSERT_THAT(status, IsOk()) << "Unable to compress file";

  off_t inpos = lseek(infd.GetValue(), 0, SEEK_END);
  EXPECT_GE(inpos, 0);

  off_t outpos = lseek(outfd.GetValue(), 0, SEEK_END);
  EXPECT_GE(outpos, 0);

  EXPECT_LT(outpos, inpos);
}

INSTANTIATE_TEST_SUITE_P(SandboxTest, TestTextFD,
                         testing::Values(ZOPFLI_FORMAT_DEFLATE,
                                         ZOPFLI_FORMAT_GZIP,
                                         ZOPFLI_FORMAT_ZLIB));

TEST_P(TestBinaryFD, Compress) {
  ZopfliSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";
  ZopfliApi api = ZopfliApi(&sandbox);

  std::string infile_s = GetTestFilePath("binary");
  std::string outfile_s = GetTemporaryFile("binary.out");
  ASSERT_THAT(outfile_s, Not(IsEmpty()));

  sapi::v::Fd infd(open(infile_s.c_str(), O_RDONLY));
  ASSERT_GE(infd.GetValue(), 0);

  sapi::v::Fd outfd(open(outfile_s.c_str(), O_WRONLY));
  ASSERT_GE(outfd.GetValue(), 0);

  absl::Status status = CompressFD(api, infd, outfd, GetParam());
  ASSERT_THAT(status, IsOk()) << "Unable to compress file";

  off_t inpos = lseek(infd.GetValue(), 0, SEEK_END);
  EXPECT_GE(inpos, 0);

  off_t outpos = lseek(outfd.GetValue(), 0, SEEK_END);
  EXPECT_GE(outpos, 0);

  EXPECT_LT(outpos, inpos);
}

INSTANTIATE_TEST_SUITE_P(SandboxTest, TestBinaryFD,
                         testing::Values(ZOPFLI_FORMAT_DEFLATE,
                                         ZOPFLI_FORMAT_GZIP,
                                         ZOPFLI_FORMAT_ZLIB));
}  // namespace
