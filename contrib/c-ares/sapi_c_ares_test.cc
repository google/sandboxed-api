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

#include "contrib/c-ares/sapi_c_ares.h"

#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/status_matchers.h"

using ::sapi::IsOk;
using ::testing::Not;
using ::testing::StrEq;

namespace {
class AresSapiSandboxTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    sandbox_ = new c_ares_sapi::AresSapiSandbox();
    ASSERT_THAT(sandbox_->Init(), IsOk());
    lib_ = new c_ares_sapi::AresApi(sandbox_);
  }
  static void TearDownTestSuite() {
    delete lib_;
    delete sandbox_;
  }
  static c_ares_sapi::AresApi* lib_;

 private:
  static c_ares_sapi::AresSapiSandbox* sandbox_;
};

c_ares_sapi::AresApi* AresSapiSandboxTest::lib_;
c_ares_sapi::AresSapiSandbox* AresSapiSandboxTest::sandbox_;
}
