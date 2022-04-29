// Copyright 2020 Google LLC
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

// Sandboxed version of simple.c
// Simple HTTP GET request

#include <cstdlib>

#include "../curl_util.h"    // NOLINT(build/include)
#include "../sandbox.h"      // NOLINT(build/include)
#include "curl_sapi.sapi.h"  // NOLINT(build/include)
#include "absl/strings/str_cat.h"
#include "sandboxed_api/util/status_macros.h"

namespace {

absl::Status Example1() {
  // Initialize sandbox2 and sapi
  curl::CurlSapiSandbox sandbox;
  SAPI_RETURN_IF_ERROR(sandbox.Init());
  curl::CurlApi api(&sandbox);

  // Initialize the curl session
  curl::CURL* curl_handle;
  SAPI_ASSIGN_OR_RETURN(curl_handle, api.curl_easy_init());
  sapi::v::RemotePtr curl(curl_handle);
  if (!curl_handle) {
    return absl::UnknownError("curl_easy_init failed: Invalid curl handle");
  }

  int curl_code;

  // Specify URL to get
  sapi::v::ConstCStr url("http://example.com");
  SAPI_ASSIGN_OR_RETURN(
      curl_code,
      api.curl_easy_setopt_ptr(&curl, curl::CURLOPT_URL, url.PtrBefore()));
  if (curl_code != 0) {
    return absl::UnknownError(absl::StrCat("curl_easy_setopt_ptr failed: ",
                                           curl::StrError(&api, curl_code)));
  }

  // Set the library to follow a redirection
  SAPI_ASSIGN_OR_RETURN(
      curl_code,
      api.curl_easy_setopt_long(&curl, curl::CURLOPT_FOLLOWLOCATION, 1l));
  if (curl_code != 0) {
    return absl::UnknownError(absl::StrCat("curl_easy_setopt_long failed: ",
                                           curl::StrError(&api, curl_code)));
  }

  // Disable authentication of peer certificate
  SAPI_ASSIGN_OR_RETURN(
      curl_code,
      api.curl_easy_setopt_long(&curl, curl::CURLOPT_SSL_VERIFYPEER, 0l));
  if (curl_code != 0) {
    return absl::UnknownError(absl::StrCat("curl_easy_setopt_long failed: ",
                                           curl::StrError(&api, curl_code)));
  }

  // Perform the request
  SAPI_ASSIGN_OR_RETURN(curl_code, api.curl_easy_perform(&curl));
  if (curl_code != 0) {
    return absl::UnknownError(absl::StrCat("curl_easy_perform failed: ",
                                           curl::StrError(&api, curl_code)));
  }

  // Cleanup curl
  SAPI_RETURN_IF_ERROR(api.curl_easy_cleanup(&curl));

  return absl::OkStatus();
}

}  // namespace

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  sapi::InitLogging(argv[0]);

  if (absl::Status status = Example1(); !status.ok()) {
    LOG(ERROR) << "Example1 failed: " << status.ToString();
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
