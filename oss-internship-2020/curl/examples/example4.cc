// Copyright 2020 Google LLC
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

// Sandboxed version of multi-poll.c
// HTTP GET request with polling

#include <cstdlib>

#include "../sandbox.h"
#include "curl_sapi.sapi.h"
#include "sandboxed_api/util/flag.h"

int main() {
  absl::Status status;
  sapi::StatusOr<int> status_or_int;
  sapi::StatusOr<CURL*> status_or_curl;
  sapi::StatusOr<CURLM*> status_or_curlm;

  // Initialize sandbox2 and sapi
  CurlSapiSandbox sandbox;
  status = sandbox.Init();
  if (!status.ok()) {
    std::cerr << "error in sandbox Init" << std::endl;
    return EXIT_FAILURE;
  }
  CurlApi api(&sandbox);

  // Number of running handles
  sapi::v::Int still_running(1);

  // Initialize curl (CURL_GLOBAL_DEFAULT = 3)
  status_or_int = api.curl_global_init(3l);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_global_init" << std::endl;
    return EXIT_FAILURE;
  }

  // Initialize http_handle
  status_or_curl = api.curl_easy_init();
  if (!status_or_curl.ok()) {
    std::cerr << "error in curl_easy_init" << std::endl;
    return EXIT_FAILURE;
  }
  sapi::v::RemotePtr http_handle(status_or_curl.value());
  if (!http_handle.GetValue()) {
    std::cerr << "error in curl_easy_init" << std::endl;
    return EXIT_FAILURE;
  }

  // Specify URL to get
  sapi::v::ConstCStr url("http://example.com");
  status_or_int =
      api.curl_easy_setopt_ptr(&http_handle, CURLOPT_URL, url.PtrBefore());
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_easy_setopt_ptr" << std::endl;
    return EXIT_FAILURE;
  }

  // Initialize multi_handle
  status_or_curlm = api.curl_multi_init();
  if (!status_or_curl.ok()) {
    std::cerr << "error in curl_multi_init" << std::endl;
    return EXIT_FAILURE;
  }
  sapi::v::RemotePtr multi_handle(status_or_curlm.value());
  if (!multi_handle.GetValue()) {
    std::cerr << "error in curl_multi_init" << std::endl;
    return EXIT_FAILURE;
  }

  // Add http_handle to the multi stack
  status_or_int = api.curl_multi_add_handle(&multi_handle, &http_handle);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_multi_add_handle" << std::endl;
    return EXIT_FAILURE;
  }

  while (still_running.GetValue()) {
    sapi::v::Int numfds(0);

    // Perform the request
    status_or_int =
        api.curl_multi_perform(&multi_handle, still_running.PtrBoth());
    if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
      std::cerr << "error in curl_multi_perform" << std::endl;
      return EXIT_FAILURE;
    }

    if (still_running.GetValue()) {
      // Wait for an event or timeout
      sapi::v::NullPtr null_ptr;
      status_or_int = api.curl_multi_poll_sapi(&multi_handle, &null_ptr, 0,
                                               1000, numfds.PtrBoth());
      if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
        std::cerr << "error in curl_multi_poll_sapi" << std::endl;
        return EXIT_FAILURE;
      }
    }
  }

  // Remove http_handle from the multi stack
  status_or_int = api.curl_multi_remove_handle(&multi_handle, &http_handle);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_multi_remove_handle" << std::endl;
    return EXIT_FAILURE;
  }

  // Cleanup http_handle
  status = api.curl_easy_cleanup(&http_handle);
  if (!status.ok()) {
    std::cerr << "error in curl_easy_cleanup" << std::endl;
    return EXIT_FAILURE;
  }

  // Cleanup multi_handle
  status_or_int = api.curl_multi_cleanup(&multi_handle);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_multi_cleanup" << std::endl;
    return EXIT_FAILURE;
  }

  // Cleanup curl
  status = api.curl_global_cleanup();
  if (!status.ok()) {
    std::cerr << "error in curl_global_cleanup" << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
