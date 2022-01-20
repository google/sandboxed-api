// Copyright 2021 Google LLC
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

#ifndef SANDBOXED_API_UTIL_OS_ERROR_H_
#define SANDBOXED_API_UTIL_OS_ERROR_H_

#include <utility>

#include "absl/strings/str_cat.h"
#include "sandboxed_api/util/strerror.h"

namespace sapi {

// Returns a formatted message with the result of `StrError(error_number)`
// appended.
template <typename... Arg>
std::string OsErrorMessage(int error_number, const Arg&... args) {
  return absl::StrCat(std::forward<const Arg&>(args)..., ": ",
                      ::sapi::StrError(error_number));
}

}  // namespace sapi

#endif  // SANDBOXED_API_UTIL_OS_ERROR_H_
