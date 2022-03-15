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

// This file is a custom fork of util/task/status_macros.h. This will become
// obsolete and will be replaced once Abseil releases absl::Status.

#ifndef THIRD_PARTY_SAPI_UTIL_STATUS_MACROS_H_
#define THIRD_PARTY_SAPI_UTIL_STATUS_MACROS_H_

#include "absl/base/optimization.h"
#include "absl/status/status.h"

// Internal helper for concatenating macro values.
#define SAPI_MACROS_IMPL_CONCAT_INNER_(x, y) x##y
#define SAPI_MACROS_IMPL_CONCAT(x, y) SAPI_MACROS_IMPL_CONCAT_INNER_(x, y)

#define SAPI_RETURN_IF_ERROR(expr)                                           \
  SAPI_RETURN_IF_ERROR_IMPL(SAPI_MACROS_IMPL_CONCAT(_sapi_status, __LINE__), \
                            expr)

#define SAPI_RETURN_IF_ERROR_IMPL(status, expr) \
  do {                                          \
    const auto status = (expr);                 \
    if (ABSL_PREDICT_FALSE(!status.ok())) {     \
      return status;                            \
    }                                           \
  } while (0)

#define SAPI_ASSIGN_OR_RETURN(lhs, rexpr) \
  SAPI_ASSIGN_OR_RETURN_IMPL(             \
      SAPI_MACROS_IMPL_CONCAT(_sapi_statusor, __LINE__), lhs, rexpr)

#define SAPI_ASSIGN_OR_RETURN_IMPL(statusor, lhs, rexpr) \
  auto statusor = (rexpr);                               \
  if (ABSL_PREDICT_FALSE(!statusor.ok())) {              \
    return statusor.status();                            \
  }                                                      \
  lhs = std::move(statusor).value()

#endif  // THIRD_PARTY_SAPI_UTIL_STATUS_MACROS_H_
