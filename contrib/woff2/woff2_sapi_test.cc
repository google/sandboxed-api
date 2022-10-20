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

#include "contrib/woff2/woff2_sapi.h"

#include <woff2/encode.h>

#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "contrib/woff2/woff2_wrapper.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"
#include "woff2_sapi.sapi.h"  // NOLINT(build/include)

namespace {

using ::sapi::IsOk;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::Not;
using ::testing::StrEq;

class Woff2SapiSandboxTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    test_data_dir_ = ::getenv("TEST_DATA_DIR");
    ASSERT_THAT(test_data_dir_, Not(IsNull()));
    sandbox_ = new ::sapi_woff2::Woff2SapiSandbox();
    ASSERT_THAT(sandbox_->Init(), IsOk());
    api_ = new ::sapi_woff2::WOFF2Api(sandbox_);
  }
  static void TearDownTestSuite() {
    delete api_;
    delete sandbox_;
  }
  static absl::StatusOr<std::vector<uint8_t>> ReadFile(
      const char* in_file, size_t expected_size = SIZE_MAX);
  static const char* test_data_dir_;
  static ::sapi_woff2::WOFF2Api* api_;

 private:
  static ::sapi_woff2::Woff2SapiSandbox* sandbox_;
};

::sapi_woff2::Woff2SapiSandbox* Woff2SapiSandboxTest::sandbox_;
::sapi_woff2::WOFF2Api* Woff2SapiSandboxTest::api_;
const char* Woff2SapiSandboxTest::test_data_dir_;

std::streamsize GetStreamSize(std::ifstream& stream) {
  stream.seekg(0, std::ios_base::end);
  std::streamsize ssize = stream.tellg();
  stream.seekg(0, std::ios_base::beg);

  return ssize;
}

absl::StatusOr<std::vector<uint8_t>> Woff2SapiSandboxTest::ReadFile(
    const char* in_file, size_t expected_size) {
  auto env = absl::StrCat(test_data_dir_, "/", in_file);
  std::ifstream f(env);
  if (!f.is_open()) {
    return absl::UnavailableError("File could not be opened");
  }
  std::streamsize ssize = GetStreamSize(f);
  if (expected_size != SIZE_MAX && ssize != expected_size) {
    return absl::UnavailableError("Incorrect size of file");
  }
  std::vector<uint8_t> inbuf((ssize));
  f.read(reinterpret_cast<char*>(inbuf.data()), ssize);
  if (ssize != f.gcount()) {
    return absl::UnavailableError("Premature end of file");
  }
  if (f.fail() || f.eof()) {
    return absl::UnavailableError("Error reading file");
  }
  return inbuf;
}

TEST_F(Woff2SapiSandboxTest, Compress) {
  auto result = ReadFile("Roboto-Regular.ttf");
  ASSERT_THAT(result, IsOk());
  sapi::v::Array array(result->data(), result->size());
  sapi::v::GenericPtr p;
  sapi::v::IntBase<size_t> out_length;
  auto compress_result = api_->WOFF2_ConvertTTFToWOFF2(
      array.PtrBefore(), result->size(), p.PtrAfter(), out_length.PtrAfter());
  ASSERT_THAT(compress_result, IsOk());
  ASSERT_TRUE(compress_result.value());
  ASSERT_THAT(p.GetValue(), Not(Eq(0)));
  auto ptr = sapi::v::RemotePtr{reinterpret_cast<void*>(p.GetValue())};
  ASSERT_THAT(api_->WOFF2_Free(&ptr), IsOk());
}

TEST_F(Woff2SapiSandboxTest, Decompress) {
  auto result = ReadFile("Roboto-Regular.woff2");
  ASSERT_THAT(result, IsOk());
  sapi::v::Array array(result->data(), result->size());
  sapi::v::GenericPtr p;
  sapi::v::IntBase<size_t> out_length;
  auto decompress_result = api_->WOFF2_ConvertWOFF2ToTTF(
      array.PtrBefore(), result->size(), p.PtrAfter(), out_length.PtrAfter(),
      1 << 25);
  ASSERT_THAT(decompress_result, IsOk());
  ASSERT_TRUE(decompress_result.value());
  ASSERT_THAT(p.GetValue(), Not(Eq(0)));
  auto ptr = sapi::v::RemotePtr{reinterpret_cast<void*>(p.GetValue())};
  ASSERT_THAT(api_->WOFF2_Free(&ptr), IsOk());
}

}  // namespace
