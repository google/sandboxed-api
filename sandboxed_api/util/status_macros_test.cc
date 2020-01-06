// Copyright 2020 Google LLC. All Rights Reserved.
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

#include "sandboxed_api/util/status_macros.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/util/status.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sandboxed_api/util/statusor.h"

namespace sapi {
namespace {

TEST(ReturnIfError, ReturnsOnErrorStatus) {
  auto func = []() -> Status {
    SAPI_RETURN_IF_ERROR(OkStatus());
    SAPI_RETURN_IF_ERROR(OkStatus());
    SAPI_RETURN_IF_ERROR(Status(sapi::StatusCode::kUnknown, "EXPECTED"));
    return Status(sapi::StatusCode::kUnknown, "ERROR");
  };

  EXPECT_THAT(func(), StatusIs(sapi::StatusCode::kUnknown, "EXPECTED"));
}

TEST(ReturnIfError, ReturnsOnErrorFromLambda) {
  auto func = []() -> Status {
    SAPI_RETURN_IF_ERROR([] { return sapi::OkStatus(); }());
    SAPI_RETURN_IF_ERROR(
        [] { return Status(sapi::StatusCode::kUnknown, "EXPECTED"); }());
    return Status(sapi::StatusCode::kUnknown, "ERROR");
  };

  EXPECT_THAT(func(), StatusIs(sapi::StatusCode::kUnknown, "EXPECTED"));
}

TEST(AssignOrReturn, AssignsMultipleVariablesInSequence) {
  auto func = []() -> Status {
    int value1;
    SAPI_ASSIGN_OR_RETURN(value1, StatusOr<int>(1));
    EXPECT_EQ(1, value1);
    int value2;
    SAPI_ASSIGN_OR_RETURN(value2, StatusOr<int>(2));
    EXPECT_EQ(2, value2);
    int value3;
    SAPI_ASSIGN_OR_RETURN(value3, StatusOr<int>(3));
    EXPECT_EQ(3, value3);
    int value4;
    SAPI_ASSIGN_OR_RETURN(
        value4, StatusOr<int>(Status(sapi::StatusCode::kUnknown, "EXPECTED")));
    return Status(sapi::StatusCode::kUnknown,
                  absl::StrCat("ERROR: assigned value ", value4));
  };

  EXPECT_THAT(func(), StatusIs(sapi::StatusCode::kUnknown, "EXPECTED"));
}

TEST(AssignOrReturn, AssignsRepeatedlyToSingleVariable) {
  auto func = []() -> Status {
    int value = 1;
    SAPI_ASSIGN_OR_RETURN(value, StatusOr<int>(2));
    EXPECT_EQ(2, value);
    SAPI_ASSIGN_OR_RETURN(value, StatusOr<int>(3));
    EXPECT_EQ(3, value);
    SAPI_ASSIGN_OR_RETURN(
        value, StatusOr<int>(Status(sapi::StatusCode::kUnknown, "EXPECTED")));
    return Status(sapi::StatusCode::kUnknown, "ERROR");
  };

  EXPECT_THAT(func(), StatusIs(sapi::StatusCode::kUnknown, "EXPECTED"));
}

TEST(AssignOrReturn, MovesUniquePtr) {
  auto func = []() -> Status {
    std::unique_ptr<int> ptr;
    SAPI_ASSIGN_OR_RETURN(
        ptr, StatusOr<std::unique_ptr<int>>(absl::make_unique<int>(1)));
    EXPECT_EQ(*ptr, 1);
    return Status(sapi::StatusCode::kUnknown, "EXPECTED");
  };

  EXPECT_THAT(func(), StatusIs(sapi::StatusCode::kUnknown, "EXPECTED"));
}

TEST(AssignOrReturn, DoesNotAssignUniquePtrOnErrorStatus) {
  auto func = []() -> Status {
    std::unique_ptr<int> ptr;
    SAPI_ASSIGN_OR_RETURN(ptr, StatusOr<std::unique_ptr<int>>(Status(
                                   sapi::StatusCode::kUnknown, "EXPECTED")));
    EXPECT_EQ(ptr, nullptr);
    return OkStatus();
  };

  EXPECT_THAT(func(), StatusIs(sapi::StatusCode::kUnknown, "EXPECTED"));
}

TEST(AssignOrReturn, MovesUniquePtrRepeatedlyToSingleVariable) {
  auto func = []() -> Status {
    std::unique_ptr<int> ptr;
    SAPI_ASSIGN_OR_RETURN(
        ptr, StatusOr<std::unique_ptr<int>>(absl::make_unique<int>(1)));
    EXPECT_EQ(*ptr, 1);
    SAPI_ASSIGN_OR_RETURN(
        ptr, StatusOr<std::unique_ptr<int>>(absl::make_unique<int>(2)));
    EXPECT_EQ(*ptr, 2);
    return Status(sapi::StatusCode::kUnknown, "EXPECTED");
  };

  EXPECT_THAT(func(), StatusIs(sapi::StatusCode::kUnknown, "EXPECTED"));
}

}  // namespace
}  // namespace sapi
