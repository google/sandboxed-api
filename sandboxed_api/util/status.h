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
// util/task/status.h. This will become obsolete and will be replaced once
// Abseil releases absl::Status.

#ifndef THIRD_PARTY_SAPI_UTIL_STATUS_H_
#define THIRD_PARTY_SAPI_UTIL_STATUS_H_

#include <ostream>
#include <string>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/util/status.pb.h"
#include "sandboxed_api/util/status_internal.h"

namespace sapi {

enum class StatusCode {
  kOk = 0,
  kCancelled = 1,
  kUnknown = 2,
  kInvalidArgument = 3,
  kDeadlineExceeded = 4,
  kNotFound = 5,
  kAlreadyExists = 6,
  kPermissionDenied = 7,
  kResourceExhausted = 8,
  kFailedPrecondition = 9,
  kAborted = 10,
  kOutOfRange = 11,
  kUnimplemented = 12,
  kInternal = 13,
  kUnavailable = 14,
  kDataLoss = 15,
  kUnauthenticated = 16,
};

namespace internal {

std::string CodeEnumToString(StatusCode code);

}  // namespace internal

class Status {
 public:
  Status();

  template <typename Enum>
  Status(Enum code, absl::string_view message) {
    Set(code, message);
  }

  Status(const Status&) = default;
  Status(Status&& other);

  template <typename StatusT,
            typename E = typename absl::enable_if_t<
                status_internal::status_type_traits<StatusT>::is_status>>
  explicit Status(const StatusT& other) {
    Set(status_internal::status_type_traits<StatusT>::CanonicalCode(other),
        other.message());
  }

  Status& operator=(const Status&) = default;
  Status& operator=(Status&& other);

  template <typename StatusT,
            typename E = typename absl::enable_if_t<
                status_internal::status_type_traits<StatusT>::is_status>>
  StatusT ToOtherStatus() {
    return StatusT(status_internal::ErrorCodeHolder(error_code_), message_);
  }

  int error_code() const { return error_code_; }
  absl::string_view error_message() const { return message_; }
  absl::string_view message() const { return message_; }
  ABSL_MUST_USE_RESULT bool ok() const { return error_code_ == 0; }
  StatusCode code() const { return static_cast<StatusCode>(error_code_); }

  std::string ToString() const;

  void IgnoreError() const {}

 private:
  template <typename Enum, typename StringViewT>
  void Set(Enum code, StringViewT message) {
    error_code_ = static_cast<int>(code);
    if (error_code_ != 0) {
      message_ = std::string(message);
    } else {
      message_.clear();
    }
  }

  int error_code_;
  std::string message_;
};

Status OkStatus();

inline bool operator==(const Status& lhs, const Status& rhs) {
  return (lhs.error_code() == rhs.error_code()) &&
         (lhs.error_message() == rhs.error_message());
}

inline bool operator!=(const Status& lhs, const Status& rhs) {
  return !(lhs == rhs);
}

// Each of the functions below creates a canonical error with the given
// message. The error code of the returned status object matches the name of
// the function.
Status AbortedError(absl::string_view message);
Status AlreadyExistsError(absl::string_view message);
Status CancelledError(absl::string_view message);
Status DataLossError(absl::string_view message);
Status DeadlineExceededError(absl::string_view message);
Status FailedPreconditionError(absl::string_view message);
Status InternalError(absl::string_view message);
Status InvalidArgumentError(absl::string_view message);
Status NotFoundError(absl::string_view message);
Status OutOfRangeError(absl::string_view message);
Status PermissionDeniedError(absl::string_view message);
Status ResourceExhaustedError(absl::string_view message);
Status UnauthenticatedError(absl::string_view message);
Status UnavailableError(absl::string_view message);
Status UnimplementedError(absl::string_view message);
Status UnknownError(absl::string_view message);

// Each of the functions below returns true if the given status matches the
// canonical error code implied by the function's name. If necessary, the
// status will be converted to the canonical error space to perform the
// comparison.
ABSL_MUST_USE_RESULT bool IsAborted(const Status& status);
ABSL_MUST_USE_RESULT bool IsAlreadyExists(const Status& status);
ABSL_MUST_USE_RESULT bool IsCancelled(const Status& status);
ABSL_MUST_USE_RESULT bool IsDataLoss(const Status& status);
ABSL_MUST_USE_RESULT bool IsDeadlineExceeded(const Status& status);
ABSL_MUST_USE_RESULT bool IsFailedPrecondition(const Status& status);
ABSL_MUST_USE_RESULT bool IsInternal(const Status& status);
ABSL_MUST_USE_RESULT bool IsInvalidArgument(const Status& status);
ABSL_MUST_USE_RESULT bool IsNotFound(const Status& status);
ABSL_MUST_USE_RESULT bool IsOutOfRange(const Status& status);
ABSL_MUST_USE_RESULT bool IsPermissionDenied(const Status& status);
ABSL_MUST_USE_RESULT bool IsResourceExhausted(const Status& status);
ABSL_MUST_USE_RESULT bool IsUnauthenticated(const Status& status);
ABSL_MUST_USE_RESULT bool IsUnavailable(const Status& status);
ABSL_MUST_USE_RESULT bool IsUnimplemented(const Status& status);
ABSL_MUST_USE_RESULT bool IsUnknown(const Status& status);

std::ostream& operator<<(std::ostream& os, const Status& status);

inline void SaveStatusToProto(const Status& status, StatusProto* out) {
  out->set_code(status.error_code());
  out->set_error_message(std::string(status.error_message()));
}

inline Status MakeStatusFromProto(const StatusProto& proto) {
  return Status(proto.code(), proto.error_message());
}

}  // namespace sapi

#endif  // THIRD_PARTY_SAPI_UTIL_STATUS_H_
