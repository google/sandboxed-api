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

#include "sandboxed_api/util/status.h"

#include "absl/strings/str_cat.h"

namespace sapi {
namespace internal {

std::string CodeEnumToString(StatusCode code) {
  switch (code) {
    case StatusCode::kOk:
      return "OK";
    case StatusCode::kCancelled:
      return "CANCELLED";
    case StatusCode::kUnknown:
      return "UNKNOWN";
    case StatusCode::kInvalidArgument:
      return "INVALID_ARGUMENT";
    case StatusCode::kDeadlineExceeded:
      return "DEADLINE_EXCEEDED";
    case StatusCode::kNotFound:
      return "NOT_FOUND";
    case StatusCode::kAlreadyExists:
      return "ALREADY_EXISTS";
    case StatusCode::kPermissionDenied:
      return "PERMISSION_DENIED";
    case StatusCode::kUnauthenticated:
      return "UNAUTHENTICATED";
    case StatusCode::kResourceExhausted:
      return "RESOURCE_EXHAUSTED";
    case StatusCode::kFailedPrecondition:
      return "FAILED_PRECONDITION";
    case StatusCode::kAborted:
      return "ABORTED";
    case StatusCode::kOutOfRange:
      return "OUT_OF_RANGE";
    case StatusCode::kUnimplemented:
      return "UNIMPLEMENTED";
    case StatusCode::kInternal:
      return "INTERNAL";
    case StatusCode::kUnavailable:
      return "UNAVAILABLE";
    case StatusCode::kDataLoss:
      return "DATA_LOSS";
  }
  return "UNKNOWN";
}

}  // namespace internal

Status::Status() : error_code_{static_cast<int>(StatusCode::kOk)} {}

Status::Status(Status&& other)
    : error_code_(other.error_code_), message_(std::move(other.message_)) {
  other.Set(StatusCode::kUnknown, "");
}

Status& Status::operator=(Status&& other) {
  error_code_ = other.error_code_;
  message_ = std::move(other.message_);
  other.Set(StatusCode::kUnknown, "");
  return *this;
}

std::string Status::ToString() const {
  return ok() ? "OK"
              : absl::StrCat("generic::",
                             internal::CodeEnumToString(
                                 static_cast<StatusCode>(error_code_)),
                             ": ", message_);
}

Status OkStatus() { return Status{}; }

std::ostream& operator<<(std::ostream& os, const Status& status) {
  return os << status.ToString();
}

Status AbortedError(absl::string_view message) {
  return Status{StatusCode::kAborted, message};
}
Status AlreadyExistsError(absl::string_view message) {
  return Status{StatusCode::kAlreadyExists, message};
}
Status CancelledError(absl::string_view message) {
  return Status{StatusCode::kCancelled, message};
}
Status DataLossError(absl::string_view message) {
  return Status{StatusCode::kDataLoss, message};
}
Status DeadlineExceededError(absl::string_view message) {
  return Status{StatusCode::kDeadlineExceeded, message};
}
Status FailedPreconditionError(absl::string_view message) {
  return Status{StatusCode::kFailedPrecondition, message};
}
Status InternalError(absl::string_view message) {
  return Status{StatusCode::kInternal, message};
}
Status InvalidArgumentError(absl::string_view message) {
  return Status{StatusCode::kInvalidArgument, message};
}
Status NotFoundError(absl::string_view message) {
  return Status{StatusCode::kNotFound, message};
}
Status OutOfRangeError(absl::string_view message) {
  return Status{StatusCode::kOutOfRange, message};
}
Status PermissionDeniedError(absl::string_view message) {
  return Status{StatusCode::kPermissionDenied, message};
}
Status ResourceExhaustedError(absl::string_view message) {
  return Status{StatusCode::kResourceExhausted, message};
}
Status UnauthenticatedError(absl::string_view message) {
  return Status{StatusCode::kUnauthenticated, message};
}
Status UnavailableError(absl::string_view message) {
  return Status{StatusCode::kUnavailable, message};
}
Status UnimplementedError(absl::string_view message) {
  return Status{StatusCode::kUnimplemented, message};
}
Status UnknownError(absl::string_view message) {
  return Status{StatusCode::kUnknown, message};
}

bool IsAborted(const Status& status) {
  return status.code() == StatusCode::kAborted;
}
bool IsAlreadyExists(const Status& status) {
  return status.code() == StatusCode::kAlreadyExists;
}
bool IsCancelled(const Status& status) {
  return status.code() == StatusCode::kCancelled;
}
bool IsDataLoss(const Status& status) {
  return status.code() == StatusCode::kDataLoss;
}
bool IsDeadlineExceeded(const Status& status) {
  return status.code() == StatusCode::kDeadlineExceeded;
}
bool IsFailedPrecondition(const Status& status) {
  return status.code() == StatusCode::kFailedPrecondition;
}
bool IsInternal(const Status& status) {
  return status.code() == StatusCode::kInternal;
}
bool IsInvalidArgument(const Status& status) {
  return status.code() == StatusCode::kInvalidArgument;
}
bool IsNotFound(const Status& status) {
  return status.code() == StatusCode::kNotFound;
}
bool IsOutOfRange(const Status& status) {
  return status.code() == StatusCode::kOutOfRange;
}
bool IsPermissionDenied(const Status& status) {
  return status.code() == StatusCode::kPermissionDenied;
}
bool IsResourceExhausted(const Status& status) {
  return status.code() == StatusCode::kResourceExhausted;
}
bool IsUnauthenticated(const Status& status) {
  return status.code() == StatusCode::kUnauthenticated;
}
bool IsUnavailable(const Status& status) {
  return status.code() == StatusCode::kUnavailable;
}
bool IsUnimplemented(const Status& status) {
  return status.code() == StatusCode::kUnimplemented;
}
bool IsUnknown(const Status& status) {
  return status.code() == StatusCode::kUnknown;
}

}  // namespace sapi
