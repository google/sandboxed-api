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
#include "utils/utils_miniz.h"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>

#include "gflags/gflags.h"
#include "glog/logging.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sapi_miniz.h"

namespace {

using ::sapi::IsOk;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::StrEq;

class MinizSapiSandboxTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    ASSERT_THAT(getenv("TEST_FILES_DIR"), NotNull());
    sandbox_ = new MinizSapiSandbox();
    ASSERT_THAT(sandbox_->Init(), IsOk());
    api_ = new miniz_sapi::MinizApi(sandbox_);
  }
  static void TearDownTestSuite() {
    delete api_;
    delete sandbox_;
  }

  static miniz_sapi::MinizApi* api_;
  static MinizSapiSandbox* sandbox_;
};

miniz_sapi::MinizApi* MinizSapiSandboxTest::api_;
MinizSapiSandbox* MinizSapiSandboxTest::sandbox_;

std::string GetTestFilePath(const std::string& filename) {
  return sapi::file::JoinPath(getenv("TEST_FILES_DIR"), filename);
}

TEST_F(MinizSapiSandboxTest, Compressor) {
  std::ifstream f(GetTestFilePath(""));
  ASSERT_TRUE(f.is_open());
  auto s = sapi::util::CompressInMemory(*api_, f, 9);
  ASSERT_THAT(s, IsOk());
}

TEST_F(MinizSapiSandboxTest, Decompressor) {
}
} // namespace

int main(int argc, char* argv[]) {
  ::google::InitGoogleLogging(program_invocation_short_name);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}