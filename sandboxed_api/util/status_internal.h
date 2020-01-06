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

#ifndef THIRD_PARTY_SAPI_UTIL_STATUS_INTERNAL_H_
#define THIRD_PARTY_SAPI_UTIL_STATUS_INTERNAL_H_

#include <string>
#include <type_traits>

#include "absl/meta/type_traits.h"

namespace sapi {
namespace status_internal {

struct ErrorCodeHolder {
  explicit ErrorCodeHolder(int code) : error_code(code) {}

  template <typename EnumT,
            typename E = typename absl::enable_if_t<std::is_enum<EnumT>::value>>
  operator EnumT() {  // NOLINT(runtime/explicit)
    return static_cast<EnumT>(error_code);
  }
  int error_code;
};

template <typename StatusT>
struct status_type_traits {
 private:
  template <typename StatusU>
  static auto CheckMinimalApi(StatusU* s, int* i, std::string* str, bool* b)
      -> decltype(StatusU(ErrorCodeHolder(0), ""), *i = s->error_code(),
                  *str = s->error_message(), *b = s->ok(), std::true_type());

  template <typename StatusU>
  static auto CheckMinimalApi(...) -> decltype(std::false_type());
  using minimal_api_type = decltype(CheckMinimalApi<StatusT>(
      static_cast<StatusT*>(0), static_cast<int*>(0),
      static_cast<std::string*>(0), static_cast<bool*>(0)));

 public:
  static constexpr bool is_status = minimal_api_type::value;
};

}  // namespace status_internal
}  // namespace sapi

#endif  // THIRD_PARTY_SAPI_UTIL_STATUS_INTERNAL_H_
