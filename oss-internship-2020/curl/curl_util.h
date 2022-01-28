// Copyright 2021 Google LLC
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

#ifndef CURL_UTIL_H_
#define CURL_UTIL_H_

#include <string>

#include "curl_sapi.sapi.h"  // NOLINT(build/include)

namespace curl {

// Calls into the sandbox to retrieve the error message for the curl error code
// in curl_error.
std::string StrError(curl::CurlApi* api, int curl_error);

}  // namespace curl

#endif  // CURL_UTIL_H_
