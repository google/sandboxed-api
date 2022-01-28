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

#include "curl_util.h"  // NOLINT(build/include)

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace curl {

std::string StrError(curl::CurlApi* api, int curl_error) {
  absl::StatusOr<void*> remote_error_message =
      api->curl_easy_strerror(static_cast<CURLcode>(curl_error));
  if (!remote_error_message.ok()) {
    return absl::StrCat("Code ", curl_error, " (curl_easy_strerror failed)");
  }

  absl::StatusOr<std::string> error_message =
      api->sandbox()->GetCString(sapi::v::RemotePtr(*remote_error_message));
  if (!error_message.ok()) {
    return absl::StrCat("Code ", curl_error, " (error getting error message)");
  }
  return *error_message;
}

}  // namespace curl
