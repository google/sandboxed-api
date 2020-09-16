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

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  absl::Status status;

  // Initialize sandbox2 and sapi
  CurlSapiSandbox sandbox;
  status = sandbox.Init();
  if (!status.ok()) {
    LOG(FATAL) << "Couldn't initialize Sandboxed API: " << status;
  }
  CurlApi api(&sandbox);

  // Number of running handles
  sapi::v::Int still_running(1);

  absl::StatusOr<int> curl_code;

  // Initialize curl (CURL_GLOBAL_DEFAULT = 3)
  curl_code = api.curl_global_init(3l);
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_global_init failed: " << curl_code.status();
  }

  // Initialize http_handle
  absl::StatusOr<CURL*> curl_handle = api.curl_easy_init();
  if (!curl_handle.ok()) {
    LOG(FATAL) << "curl_easy_init failed: " << curl_handle.status();
  }
  sapi::v::RemotePtr http_handle(curl_handle.value());
  if (!http_handle.GetValue()) {
    LOG(FATAL) << "curl_easy_init failed: http_handle is NULL";
  }

  // Specify URL to get
  sapi::v::ConstCStr url("http://example.com");
  curl_code =
      api.curl_easy_setopt_ptr(&http_handle, CURLOPT_URL, url.PtrBefore());
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_setopt_ptr failed: " << curl_code.status();
  }

  // Initialize multi_handle
  absl::StatusOr<CURLM*> curlm_handle = api.curl_multi_init();
  if (!curlm_handle.ok()) {
    LOG(FATAL) << "curl_multi_init failed: " << curlm_handle.status();
  }
  sapi::v::RemotePtr multi_handle(curlm_handle.value());
  if (!multi_handle.GetValue()) {
    LOG(FATAL) << "curl_multi_init failed: multi_handle is NULL";
  }

  // Add http_handle to the multi stack
  curl_code = api.curl_multi_add_handle(&multi_handle, &http_handle);
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_multi_add_handle failed: " << curl_code.status();
  }

  while (still_running.GetValue()) {
    sapi::v::Int numfds(0);

    // Perform the request
    curl_code = api.curl_multi_perform(&multi_handle, still_running.PtrBoth());
    if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
      LOG(FATAL) << "curl_mutli_perform failed: " << curl_code.status();
    }

    if (still_running.GetValue()) {
      // Wait for an event or timeout
      sapi::v::NullPtr null_ptr;
      curl_code = api.curl_multi_poll_sapi(&multi_handle, &null_ptr, 0, 1000,
                                           numfds.PtrBoth());
      if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
        LOG(FATAL) << "curl_multi_poll_sapi failed: " << curl_code.status();
      }
    }
  }

  // Remove http_handle from the multi stack
  curl_code = api.curl_multi_remove_handle(&multi_handle, &http_handle);
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_multi_remove_handle failed: " << curl_code.status();
  }

  // Cleanup http_handle
  status = api.curl_easy_cleanup(&http_handle);
  if (!status.ok()) {
    LOG(FATAL) << "curl_easy_cleanup failed: " << status;
  }

  // Cleanup multi_handle
  curl_code = api.curl_multi_cleanup(&multi_handle);
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_multi_cleanup failed: " << curl_code.status();
  }

  // Cleanup curl
  status = api.curl_global_cleanup();
  if (!status.ok()) {
    LOG(FATAL) << "curl_global_cleanup failed: " << status;
  }

  return EXIT_SUCCESS;
}
