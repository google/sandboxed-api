// Copyright 2019 Google LLC
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

#ifndef SANDBOXED_API_UTIL_STATUS_MATCHERS_H_
#define SANDBOXED_API_UTIL_STATUS_MATCHERS_H_

#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
// Temporary include to avoid breaking changes when moving to Abseil's matchers.
#include "absl/status/status_matchers.h"  // IWYU pragma: export
#include "absl/status/statusor.h"         // IWYU pragma: keep
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "sandboxed_api/util/status_macros.h"  // IWYU pragma: keep

#define SAPI_ASSERT_OK(expr) ASSERT_THAT(expr, ::absl_testing::IsOk())

#define SAPI_ASSERT_OK_AND_ASSIGN(lhs, rexpr) \
  SAPI_ASSERT_OK_AND_ASSIGN_IMPL(             \
      SAPI_MACROS_IMPL_CONCAT(_sapi_statusor, __LINE__), lhs, rexpr)

#define SAPI_ASSERT_OK_AND_ASSIGN_IMPL(statusor, lhs, rexpr) \
  auto statusor = (rexpr);                                   \
  ASSERT_THAT(statusor.status(), ::sapi::IsOk());            \
  lhs = std::move(statusor).value()

namespace sapi {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

}  // namespace sapi

#endif  // SANDBOXED_API_UTIL_STATUS_MATCHERS_H_
