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

// This file and it's implementation provide a custom fork of
// util/task/statusor.h. This will become obsolete and will be replaced once
// Abseil releases absl::Status.

#ifndef THIRD_PARTY_SAPI_UTIL_STATUSOR_H_
#define THIRD_PARTY_SAPI_UTIL_STATUSOR_H_

#include "absl/base/internal/raw_logging.h"
#include "absl/base/attributes.h"
#include "absl/base/log_severity.h"
#include "absl/types/variant.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/status.h"

namespace sapi {

template <typename T>
class StatusOr {
 public:
  explicit StatusOr() : variant_{Status{StatusCode::kUnknown, ""}} {}

  StatusOr(const Status& status) : variant_{status} { EnsureNotOk(); }

  StatusOr& operator=(const Status& status) {
    variant_ = status;
    EnsureNotOk();
  }

  StatusOr(const T& value) : variant_{value} {}
  StatusOr(T&& value) : variant_{std::move(value)} {}

  StatusOr(const StatusOr&) = default;
  StatusOr& operator=(const StatusOr&) = default;

  StatusOr(StatusOr&&) = default;
  StatusOr& operator=(StatusOr&&) = default;

  template <typename U>
  StatusOr(const StatusOr<U>& other) {
    *this = other;
  }

  template <typename U>
  StatusOr& operator=(const StatusOr<U>& other) {
    if (other.ok()) {
      variant_ = other.ValueOrDie();
    } else {
      variant_ = other.status();
    }
    return *this;
  }

  explicit operator bool() const { return ok(); }
  ABSL_MUST_USE_RESULT bool ok() const {
    return absl::holds_alternative<T>(variant_);
  }

  Status status() const {
    return ok() ? OkStatus() : absl::get<Status>(variant_);
  }

  const T& ValueOrDie() const& {
    EnsureOk();
    return absl::get<T>(variant_);
  }

  T& ValueOrDie() & {
    EnsureOk();
    return absl::get<T>(variant_);
  }

  T ValueOrDie() && {
    EnsureOk();
    T tmp(std::move(absl::get<T>(variant_)));
    return std::move(tmp);
  }

 private:
  void EnsureOk() const {
    if (!ok()) {
      // GoogleTest needs this exact error message for death tests to work.
      SAPI_RAW_LOG(FATAL,
                   "Attempting to fetch value instead of handling error %s",
                   status().message());
    }
  }

  void EnsureNotOk() const {
    if (ok()) {
      SAPI_RAW_LOG(
          FATAL,
          "An OK status is not a valid constructor argument to StatusOr<T>");
    }
  }

  absl::variant<Status, T> variant_;
};

}  // namespace sapi

#endif  // THIRD_PARTY_SAPI_UTIL_STATUSOR_H_
