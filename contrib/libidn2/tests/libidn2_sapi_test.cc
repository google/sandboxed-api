// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <optional>
#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "../libidn2_sapi.h"

using testing::StrEq;

class Idn2SapiSandboxTest : public testing::Test {
 protected:
  static std::optional<IDN2Lib> lib_;
  static void SetUpTestSuite() {
    auto& sandbox = sandbox_.emplace();
    ASSERT_TRUE(sandbox.Init().ok());
    lib_.emplace(&sandbox);
  }

  static void TearDownTestSuite() {
    sandbox_.reset();

  }

 private:
  static std::optional<Idn2SapiSandbox> sandbox_;
};

std::optional<IDN2Lib> Idn2SapiSandboxTest::lib_;
std::optional<Idn2SapiSandbox> Idn2SapiSandboxTest::sandbox_;

TEST_F(Idn2SapiSandboxTest, WorksOkay) {
  EXPECT_THAT(lib_->idn2_lookup_u8("β").value(), StrEq("xn--nxa"));
  EXPECT_THAT(lib_->idn2_lookup_u8("ß").value(), StrEq("xn--zca"));
  EXPECT_THAT(lib_->idn2_lookup_u8("straße.de").value(),
              StrEq("xn--strae-oqa.de"));
  EXPECT_THAT(lib_->idn2_to_unicode_8z8z("xn--strae-oqa.de").value(),
              StrEq("straße.de"));
}

TEST_F(Idn2SapiSandboxTest, RegisterConversion) {
  // I could not get this to succeed except on ASCII-only strings
  EXPECT_FALSE(lib_->idn2_register_u8("β.gr", nullptr).ok());
  EXPECT_FALSE(lib_->idn2_lookup_u8("--- ").ok());
}
