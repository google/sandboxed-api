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

// This file and it's implementation provide a custom fork of
// util/task/statusor.h. This will become obsolete and will be replaced once
// Abseil releases absl::Status.

#ifndef THIRD_PARTY_SAPI_UTIL_STATUSOR_H_
#define THIRD_PARTY_SAPI_UTIL_STATUSOR_H_

#include <initializer_list>
#include <utility>

#include "absl/base/internal/raw_logging.h"
#include "absl/base/attributes.h"
#include "absl/base/log_severity.h"
#include "absl/status/status.h"
#include "absl/types/variant.h"
#include "sandboxed_api/util/raw_logging.h"

namespace sapi {

template <typename T>
class ABSL_MUST_USE_RESULT StatusOr {
  template <typename U>
  friend class StatusOr;

 public:
  using element_type = T;

  explicit StatusOr() : variant_(absl::UnknownError("")) {}

  StatusOr(const StatusOr&) = default;
  StatusOr& operator=(const StatusOr&) = default;

  StatusOr(StatusOr&&) = default;
  StatusOr& operator=(StatusOr&&) = default;

  template <typename U>
  StatusOr(const StatusOr<U>& other) : StatusOr(other) {}

  template <typename U>
  StatusOr(StatusOr<U>&& other) : StatusOr(other) {}

  template <typename U>
  StatusOr& operator=(const StatusOr<U>& other) {
    variant_ = other.ok() ? other.value() : other.status();
    return *this;
  }

  template <typename U>
  StatusOr& operator=(StatusOr<U>&& other) {
    variant_ =
        other.ok() ? std::move(other).value() : std::move(other).status();
    return *this;
  }

  StatusOr(const T& value) : variant_(value) {}

  StatusOr(const absl::Status& status) : variant_(status) { EnsureNotOk(); }

  // Not implemented:
  // template <typename U = T>
  // StatusOr& operator=(U&& value);

  StatusOr(T&& value) : variant_(std::move(value)) {}

  StatusOr(absl::Status&& value) : variant_(std::move(value)) {}

  StatusOr& operator=(absl::Status&& status) {
    variant_ = std::move(status);
    EnsureNotOk();
  }

  template <typename... Args>
  explicit StatusOr(absl::in_place_t, Args&&... args)
      : StatusOr(T(std::forward<Args>(args)...)) {}

  template <typename U, typename... Args>
  explicit StatusOr(absl::in_place_t, std::initializer_list<U> ilist,
                    Args&&... args)
      : StatusOr(ilist, U(std::forward<Args>(args)...)) {}

  explicit operator bool() const { return ok(); }

  ABSL_MUST_USE_RESULT bool ok() const {
    return absl::holds_alternative<T>(variant_);
  }

  const absl::Status& status() const& {
    static const auto* ok_status = new absl::Status();
    return ok() ? *ok_status : absl::get<absl::Status>(variant_);
  }

  absl::Status status() && {
    return ok() ? absl::OkStatus()
                : std::move(absl::get<absl::Status>(variant_));
  }

  const T& value() const& {
    EnsureOk();
    return absl::get<T>(variant_);
  }

  T& value() & {
    EnsureOk();
    return absl::get<T>(variant_);
  }

  const T&& value() const&& {
    EnsureOk();
    return std::move(absl::get<T>(variant_));
  }

  T&& value() && {
    EnsureOk();
    return std::move(absl::get<T>(variant_));
  }

  const T& ValueOrDie() const& {
    EnsureOk();
    return absl::get<T>(variant_);
  }

  T& ValueOrDie() & {
    EnsureOk();
    return absl::get<T>(variant_);
  }

  T&& ValueOrDie() && {
    EnsureOk();
    return std::move(absl::get<T>(variant_));
  }

  const T& operator*() const& {
    EnsureOk();
    return absl::get<T>(variant_);
  }

  T& operator*() & {
    EnsureOk();
    return absl::get<T>(variant_);
  }

  const T&& operator*() const&& {
    EnsureOk();
    return std::move(absl::get<T>(variant_));
  }

  T&& operator*() && {
    EnsureOk();
    return std::move(absl::get<T>(variant_));
  }

  const T* operator->() const {
    EnsureOk();
    return &absl::get<T>(variant_);
  }

  T* operator->() {
    EnsureOk();
    return &absl::get<T>(variant_);
  }

  template <typename U>
  T value_or(U&& default_value) const& {
    if (ok()) {
      return absl::get<T>(variant_);
    }
    return std::forward<U>(default_value);
  }

  template <typename U>
  T value_or(U&& default_value) && {
    if (ok()) {
      return std::move(absl::get<T>(variant_));
    }
    return std::forward<U>(default_value);
  }

  void IgnoreError() const { /* no-op */
  }

  template <typename... Args>
  T& emplace(Args&&... args) {
    return variant_.template emplace<T>(std::forward<Args>(args)...);
  }

  template <typename U, typename... Args>
  T& emplace(std::initializer_list<U> ilist, Args&&... args) {
    return variant_.template emplace<T>(ilist, std::forward<Args>(args)...);
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

  absl::variant<absl::Status, T> variant_;
};

}  // namespace sapi

#endif  // THIRD_PARTY_SAPI_UTIL_STATUSOR_H_
