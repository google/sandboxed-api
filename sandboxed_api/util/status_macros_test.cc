// Copyright 2019 Google LLC
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
  auto func = []() -> absl::Status {
    SAPI_RETURN_IF_ERROR(absl::OkStatus());
    SAPI_RETURN_IF_ERROR(absl::OkStatus());
    SAPI_RETURN_IF_ERROR(absl::Status(absl::StatusCode::kUnknown, "EXPECTED"));
    return absl::Status(absl::StatusCode::kUnknown, "ERROR");
  };

  EXPECT_THAT(func(), StatusIs(absl::StatusCode::kUnknown, "EXPECTED"));
}

TEST(ReturnIfError, ReturnsOnErrorFromLambda) {
  auto func = []() -> absl::Status {
    SAPI_RETURN_IF_ERROR([] { return absl::OkStatus(); }());
    SAPI_RETURN_IF_ERROR(
        [] { return absl::Status(absl::StatusCode::kUnknown, "EXPECTED"); }());
    return absl::Status(absl::StatusCode::kUnknown, "ERROR");
  };

  EXPECT_THAT(func(), StatusIs(absl::StatusCode::kUnknown, "EXPECTED"));
}

TEST(AssignOrReturn, AssignsMultipleVariablesInSequence) {
  auto func = []() -> absl::Status {
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
    SAPI_ASSIGN_OR_RETURN(value4, StatusOr<int>(absl::Status(
                                      absl::StatusCode::kUnknown, "EXPECTED")));
    return absl::Status(absl::StatusCode::kUnknown,
                        absl::StrCat("ERROR: assigned value ", value4));
  };

  EXPECT_THAT(func(), StatusIs(absl::StatusCode::kUnknown, "EXPECTED"));
}

TEST(AssignOrReturn, AssignsRepeatedlyToSingleVariable) {
  auto func = []() -> absl::Status {
    int value = 1;
    SAPI_ASSIGN_OR_RETURN(value, StatusOr<int>(2));
    EXPECT_EQ(2, value);
    SAPI_ASSIGN_OR_RETURN(value, StatusOr<int>(3));
    EXPECT_EQ(3, value);
    SAPI_ASSIGN_OR_RETURN(value, StatusOr<int>(absl::Status(
                                     absl::StatusCode::kUnknown, "EXPECTED")));
    return absl::Status(absl::StatusCode::kUnknown, "ERROR");
  };

  EXPECT_THAT(func(), StatusIs(absl::StatusCode::kUnknown, "EXPECTED"));
}

TEST(AssignOrReturn, MovesUniquePtr) {
  auto func = []() -> absl::Status {
    std::unique_ptr<int> ptr;
    SAPI_ASSIGN_OR_RETURN(
        ptr, StatusOr<std::unique_ptr<int>>(absl::make_unique<int>(1)));
    EXPECT_EQ(*ptr, 1);
    return absl::Status(absl::StatusCode::kUnknown, "EXPECTED");
  };

  EXPECT_THAT(func(), StatusIs(absl::StatusCode::kUnknown, "EXPECTED"));
}

TEST(AssignOrReturn, DoesNotAssignUniquePtrOnErrorStatus) {
  auto func = []() -> absl::Status {
    std::unique_ptr<int> ptr;
    SAPI_ASSIGN_OR_RETURN(ptr, StatusOr<std::unique_ptr<int>>(absl::Status(
                                   absl::StatusCode::kUnknown, "EXPECTED")));
    EXPECT_EQ(ptr, nullptr);
    return absl::OkStatus();
  };

  EXPECT_THAT(func(), StatusIs(absl::StatusCode::kUnknown, "EXPECTED"));
}

TEST(AssignOrReturn, MovesUniquePtrRepeatedlyToSingleVariable) {
  auto func = []() -> absl::Status {
    std::unique_ptr<int> ptr;
    SAPI_ASSIGN_OR_RETURN(
        ptr, StatusOr<std::unique_ptr<int>>(absl::make_unique<int>(1)));
    EXPECT_EQ(*ptr, 1);
    SAPI_ASSIGN_OR_RETURN(
        ptr, StatusOr<std::unique_ptr<int>>(absl::make_unique<int>(2)));
    EXPECT_EQ(*ptr, 2);
    return absl::Status(absl::StatusCode::kUnknown, "EXPECTED");
  };

  EXPECT_THAT(func(), StatusIs(absl::StatusCode::kUnknown, "EXPECTED"));
}

}  // namespace
}  // namespace sapi
