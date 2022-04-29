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

// Sandboxed version of multi-poll.c
// HTTP GET request with polling

#include <cstdlib>

#include "../curl_util.h"    // NOLINT(build/include)
#include "../sandbox.h"      // NOLINT(build/include)
#include "curl_sapi.sapi.h"  // NOLINT(build/include)
#include "absl/strings/str_cat.h"
#include "sandboxed_api/util/status_macros.h"

namespace {

absl::Status Example4() {
  // Initialize sandbox2 and sapi
  curl::CurlSapiSandbox sandbox;
  SAPI_RETURN_IF_ERROR(sandbox.Init());
  curl::CurlApi api(&sandbox);

  // Number of running handles
  sapi::v::Int still_running(1);

  int curl_code;

  // Initialize curl (CURL_GLOBAL_DEFAULT = 3)
  SAPI_ASSIGN_OR_RETURN(curl_code, api.curl_global_init(3l));
  if (curl_code != 0) {
    return absl::UnavailableError(absl::StrCat(
        "curl_global_init failed: ", curl::StrError(&api, curl_code)));
  }

  // Initialize http_handle
  curl::CURL* curl_handle;
  SAPI_ASSIGN_OR_RETURN(curl_handle, api.curl_easy_init());
  sapi::v::RemotePtr http_handle(curl_handle);
  if (!curl_handle) {
    return absl::UnavailableError("curl_easy_init failed: Invalid curl handle");
  }

  // Specify URL to get
  sapi::v::ConstCStr url("http://example.com");
  SAPI_ASSIGN_OR_RETURN(
      curl_code, api.curl_easy_setopt_ptr(&http_handle, curl::CURLOPT_URL,
                                          url.PtrBefore()));
  if (curl_code != 0) {
    return absl::UnavailableError(absl::StrCat(
        "curl_easy_setopt_ptr failed: ", curl::StrError(&api, curl_code)));
  }

  // Initialize multi_handle
  curl::CURLM* curlm_handle;
  SAPI_ASSIGN_OR_RETURN(curlm_handle, api.curl_multi_init());
  sapi::v::RemotePtr multi_handle(curlm_handle);
  if (!curlm_handle) {
    return absl::UnavailableError(
        "curl_multi_init failed: multi_handle is invalid");
  }

  // Add http_handle to the multi stack
  SAPI_ASSIGN_OR_RETURN(curl_code,
                        api.curl_multi_add_handle(&multi_handle, &http_handle));
  if (curl_code != 0) {
    return absl::UnavailableError(absl::StrCat(
        "curl_multi_add_handle failed: ", curl::StrError(&api, curl_code)));
  }

  while (still_running.GetValue()) {
    sapi::v::Int numfds(0);

    // Perform the request
    SAPI_ASSIGN_OR_RETURN(
        curl_code,
        api.curl_multi_perform(&multi_handle, still_running.PtrBoth()));
    if (curl_code != 0) {
      return absl::UnavailableError(absl::StrCat(
          "curl_mutli_perform failed: ", curl::StrError(&api, curl_code)));
    }

    if (still_running.GetValue()) {
      // Wait for an event or timeout
      sapi::v::NullPtr null_ptr;
      SAPI_ASSIGN_OR_RETURN(
          curl_code, api.curl_multi_poll_sapi(&multi_handle, &null_ptr, 0, 1000,
                                              numfds.PtrBoth()));
      if (curl_code != 0) {
        return absl::UnavailableError(absl::StrCat(
            "curl_multi_poll_sapi failed: ", curl::StrError(&api, curl_code)));
      }
    }
  }

  // Remove http_handle from the multi stack
  SAPI_ASSIGN_OR_RETURN(
      curl_code, api.curl_multi_remove_handle(&multi_handle, &http_handle));
  if (curl_code != 0) {
    return absl::UnavailableError(absl::StrCat(
        "curl_multi_remove_handle failed: ", curl::StrError(&api, curl_code)));
  }

  // Cleanup http_handle
  SAPI_RETURN_IF_ERROR(api.curl_easy_cleanup(&http_handle));

  // Cleanup multi_handle
  SAPI_ASSIGN_OR_RETURN(curl_code, api.curl_multi_cleanup(&multi_handle));
  if (curl_code != 0) {
    return absl::UnavailableError(absl::StrCat(
        "curl_multi_cleanup failed: ", curl::StrError(&api, curl_code)));
  }

  // Cleanup curl
  SAPI_RETURN_IF_ERROR(api.curl_global_cleanup());

  return absl::OkStatus();
}

}  // namespace

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  sapi::InitLogging(argv[0]);

  if (absl::Status status = Example4(); !status.ok()) {
    LOG(ERROR) << "Example4 failed: " << status.ToString();
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
