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

#define _GNU_SOURCE 1
#include "../turbojpeg_sapi.h"  // NOLINT(build/include)

#include <turbojpeg.h>

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"
#include "turbojpeg_sapi.sapi.h"  // NOLINT(build/include)

namespace {

using ::sapi::IsOk;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::StrEq;

class TurboJpegSapiSandboxTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    ASSERT_THAT(getenv("TEST_FILES_DIR"), NotNull());
    sandbox_ = new TurboJpegSapiSandbox();
    ASSERT_THAT(sandbox_->Init(), IsOk());
    api_ = new turbojpeg_sapi::TurboJPEGApi(sandbox_);
  }
  static void TearDownTestSuite() {
    delete api_;
    delete sandbox_;
  }

  static std::string GetTurboJpegErrorStr(sapi::v::Ptr* handle) {
    auto errmsg_ptr = api_->tjGetErrorStr2(handle);
    if (!errmsg_ptr.ok()) return "Error getting error message";
    auto errmsg =
        sandbox_->GetCString(sapi::v::RemotePtr(errmsg_ptr.value()), 256);
    if (!errmsg.ok()) return "Error getting error message";
    return errmsg.value();
  }
  static turbojpeg_sapi::TurboJPEGApi* api_;
  static TurboJpegSapiSandbox* sandbox_;
};

turbojpeg_sapi::TurboJPEGApi* TurboJpegSapiSandboxTest::api_;
TurboJpegSapiSandbox* TurboJpegSapiSandboxTest::sandbox_;

std::string GetTestFilePath(const std::string& filename) {
  return sapi::file::JoinPath(getenv("TEST_FILES_DIR"), filename);
}

std::streamsize GetStreamSize(std::ifstream& stream) {
  stream.seekg(0, std::ios_base::end);
  std::streamsize ssize = stream.tellg();
  stream.seekg(0, std::ios_base::beg);

  return ssize;
}

absl::StatusOr<std::vector<uint8_t>> ReadFile(const std::string& in_file,
                                              size_t expected_size = SIZE_MAX) {
  std::ifstream f(GetTestFilePath(in_file));
  if (!f.is_open()) {
    return absl::UnavailableError("File could not be opened");
  }
  std::streamsize ssize = GetStreamSize(f);
  if (expected_size != SIZE_MAX && ssize != expected_size) {
    return absl::UnavailableError("Incorrect size of file");
  }
  std::vector<uint8_t> inbuf(ssize);
  f.read(reinterpret_cast<char*>(inbuf.data()), ssize);
  if (ssize != f.gcount()) {
    return absl::UnavailableError("Premature end of file");
  }
  if (f.fail() || f.eof()) {
    return absl::UnavailableError("Error reading file");
  }
  return inbuf;
}

TEST_F(TurboJpegSapiSandboxTest, Compressor) {
  absl::StatusOr<void*> compression_handle_raw = api_->tjInitCompress();
  ASSERT_THAT(compression_handle_raw, IsOk());
  ASSERT_THAT(compression_handle_raw.value(), NotNull());
  sapi::v::RemotePtr compression_handle{compression_handle_raw.value()};
  auto result = ReadFile("sample.rgb", 12 * 67 * 3);
  ASSERT_THAT(result, IsOk());
  sapi::v::Array array(result->data(), result->size());

  sapi::v::GenericPtr buffer;
  {
    sapi::v::ULong length{0};
    auto result = api_->tjCompress2(&compression_handle, array.PtrBefore(), 12,
                                    36, 67, TJPF_RGB, buffer.PtrAfter(),
                                    length.PtrBoth(), TJSAMP_444, 10, 0);
    ASSERT_THAT(result, IsOk());
    ASSERT_THAT(result.value(), Eq(0))
        << "Error from sandboxee: "
        << GetTurboJpegErrorStr(&compression_handle);
    ASSERT_TRUE(buffer.GetValue());
    ASSERT_TRUE(buffer.GetRemote());
    ASSERT_THAT(length.GetValue(), Gt(0));
  }
  auto value = buffer.GetValue();

  auto destroy_result = api_->tjDestroy(&compression_handle);
  ASSERT_THAT(destroy_result, IsOk());
  ASSERT_THAT(destroy_result.value(), Eq(0));
}

TEST_F(TurboJpegSapiSandboxTest, Decompressor) {
  absl::StatusOr<void*> decompression_handle_raw = api_->tjInitDecompress();
  ASSERT_THAT(decompression_handle_raw, IsOk());
  ASSERT_THAT(decompression_handle_raw.value(), NotNull());
  sapi::v::RemotePtr decompression_handle{decompression_handle_raw.value()};
  auto result = ReadFile("sample.jpeg");
  ASSERT_THAT(result, IsOk());
  sapi::v::Array array(result->data(), result->size());

  sapi::v::Int width{0};
  sapi::v::Int height{0};
  sapi::v::Int subsamp{0};
  sapi::v::Int colorspace{0};
  auto decompress_result = api_->tjDecompressHeader3(
      &decompression_handle, array.PtrBefore(), result->size(),
      width.PtrAfter(), height.PtrAfter(), subsamp.PtrAfter(),
      colorspace.PtrAfter());
  ASSERT_THAT(decompress_result, IsOk());
  ASSERT_THAT(decompress_result.value(), Eq(0))
      << "Error from sandboxee: "
      << GetTurboJpegErrorStr(&decompression_handle);

  ASSERT_THAT(width.GetValue(), Eq(67));
  ASSERT_THAT(height.GetValue(), Eq(12));
  ASSERT_THAT(subsamp.GetValue(), Eq(TJSAMP_GRAY));
  ASSERT_THAT(colorspace.GetValue(), Eq(TJCS_GRAY));

  auto arr = sapi::v::Array<unsigned char>(12 * 67 * 3);
  decompress_result = api_->tjDecompress2(
      &decompression_handle, array.PtrBefore(), result->size(), arr.PtrAfter(),
      12, 36, 67, TJCS_RGB, 0);
  ASSERT_THAT(decompress_result, IsOk());
  EXPECT_THAT(decompress_result.value(), Eq(0))
      << "Error from sandboxee: "
      << GetTurboJpegErrorStr(&decompression_handle);

  decompress_result = api_->tjDestroy(&decompression_handle);
  ASSERT_THAT(decompress_result, IsOk());
  ASSERT_THAT(decompress_result.value(), Eq(0));
}
}  // namespace
