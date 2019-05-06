// Copyright 2019 Google LLC. All Rights Reserved.
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

#ifndef SANDBOXED_API_UTIL_STATUS_MATCHERS_H_
#define SANDBOXED_API_UTIL_STATUS_MATCHERS_H_

#include <type_traits>

#include "gmock/gmock.h"
#include "absl/types/optional.h"
#include "sandboxed_api/util/status.h"
#include "sandboxed_api/util/status_macros.h"
#include "sandboxed_api/util/statusor.h"

#define SAPI_ASSERT_OK_AND_ASSIGN(lhs, rexpr) \
  SAPI_ASSERT_OK_AND_ASSIGN_IMPL(             \
      SAPI_MACROS_IMPL_CONCAT(_sapi_statusor, __LINE__), lhs, rexpr)

#define SAPI_ASSERT_OK_AND_ASSIGN_IMPL(statusor, lhs, rexpr) \
  auto statusor = (rexpr);                                   \
  ASSERT_THAT(statusor.status(), sapi::IsOk());              \
  lhs = std::move(statusor).ValueOrDie();

namespace sapi {
namespace internal {

class IsOkMatcher {
 public:
  template <typename StatusT>
  bool MatchAndExplain(const StatusT& status_container,
                       ::testing::MatchResultListener* listener) const {
    if (!status_container.ok()) {
      *listener << "which is not OK";
      return false;
    }
    return true;
  }

  void DescribeTo(std::ostream* os) const { *os << "is OK"; }

  void DescribeNegationTo(std::ostream* os) const { *os << "is not OK"; }
};

template <typename Enum>
class StatusIsMatcher {
 public:
  StatusIsMatcher(const StatusIsMatcher&) = default;
  StatusIsMatcher& operator=(const StatusIsMatcher&) = default;

  StatusIsMatcher(Enum code, absl::optional<absl::string_view> message)
      : code_{code}, message_{message} {}

  template <typename T>
  bool MatchAndExplain(const T& value,
                       ::testing::MatchResultListener* listener) const {
    auto status = GetStatus(value);
    if (code_ != status.code()) {
      *listener << "whose error code is generic::"
                << internal::CodeEnumToString(status.code());
      return false;
    }
    if (message_.has_value() && status.error_message() != message_.value()) {
      *listener << "whose error message is '" << message_.value() << "'";
      return false;
    }
    return true;
  }

  void DescribeTo(std::ostream* os) const {
    *os << "has a status code that is generic::"
        << internal::CodeEnumToString(code_);
    if (message_.has_value()) {
      *os << ", and has an error message that is '" << message_.value() << "'";
    }
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "has a status code that is not generic::"
        << internal::CodeEnumToString(code_);
    if (message_.has_value()) {
      *os << ", and has an error message that is not '" << message_.value()
          << "'";
    }
  }

 private:
  template <typename StatusT,
            typename std::enable_if<
                !std::is_void<decltype(std::declval<StatusT>().code())>::value,
                int>::type = 0>
  static const StatusT& GetStatus(const StatusT& status) {
    return status;
  }

  template <typename StatusOrT,
            typename StatusT = decltype(std::declval<StatusOrT>().status())>
  static StatusT GetStatus(const StatusOrT& status_or) {
    return status_or.status();
  }

  const Enum code_;
  const absl::optional<std::string> message_;
};

}  // namespace internal

inline ::testing::PolymorphicMatcher<internal::IsOkMatcher> IsOk() {
  return ::testing::MakePolymorphicMatcher(internal::IsOkMatcher{});
}

template <typename Enum>
::testing::PolymorphicMatcher<internal::StatusIsMatcher<Enum>> StatusIs(
    Enum code, absl::optional<absl::string_view> message = absl::nullopt) {
  return ::testing::MakePolymorphicMatcher(
      internal::StatusIsMatcher<Enum>{code, message});
}

}  // namespace sapi

#endif  // SANDBOXED_API_UTIL_STATUS_MATCHERS_H_
