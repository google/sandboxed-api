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

#include "contrib/tesseract-ocr/sapi_tesseract.h"

#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/status_matchers.h"

using ::sapi::IsOk;
using ::testing::Not;
using ::testing::StrEq;

namespace {
class TessSapiSandboxTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    sandbox_ = new tess_sapi::TessSapiSandbox();
    ASSERT_THAT(sandbox_->Init(), IsOk());
    lib_ = new tess_sapi::TessApi(sandbox_);
  }
  static void TearDownTestSuite() {
    delete lib_;
    delete sandbox_;
  }
  static tess_sapi::TessApi* lib_;

 private:
  static tess_sapi::TessSapiSandbox* sandbox_;
};

tess_sapi::TessApi* TessSapiSandboxTest::lib_;
tess_sapi::TessSapiSandbox* TessSapiSandboxTest::sandbox_;
}

