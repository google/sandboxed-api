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

#include "contrib/libidn2/libidn2_sapi.h"

#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/status_matchers.h"

using ::sapi::IsOk;
using ::testing::Not;
using ::testing::StrEq;

class Idn2SapiSandboxTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    sandbox_ = new Idn2SapiSandbox();
    ASSERT_THAT(sandbox_->Init(), IsOk());
    lib_ = new IDN2Lib(sandbox_);
  }
  static void TearDownTestSuite() {
    delete lib_;
    delete sandbox_;
  }
  static IDN2Lib* lib_;

 private:
  static Idn2SapiSandbox* sandbox_;
};

IDN2Lib* Idn2SapiSandboxTest::lib_;
Idn2SapiSandbox* Idn2SapiSandboxTest::sandbox_;

TEST_F(Idn2SapiSandboxTest, WorksOkay) {
  EXPECT_THAT(lib_->idn2_lookup_u8("β").value(), StrEq("xn--nxa"));
  EXPECT_THAT(lib_->idn2_lookup_u8("ß").value(), StrEq("xn--zca"));
  EXPECT_THAT(lib_->idn2_lookup_u8("straße.de").value(),
              StrEq("xn--strae-oqa.de"));
  EXPECT_THAT(lib_->idn2_to_unicode_8z8z("xn--strae-oqa.de").value(),
              StrEq("straße.de"));
  EXPECT_THAT(lib_->idn2_lookup_u8("--- "), Not(IsOk()));
}

TEST_F(Idn2SapiSandboxTest, RegisterConversion) {
  // I could not get this to succeed except on ASCII-only strings
  EXPECT_THAT(lib_->idn2_register_u8("βgr", "xn--gr-e9b").value(),
              StrEq("xn--gr-e9b"));
  EXPECT_THAT(lib_->idn2_register_u8("βgr", "xn--gr-e9"), Not(IsOk()));
  EXPECT_THAT(lib_->idn2_register_u8("β.gr", nullptr), Not(IsOk()));
}
