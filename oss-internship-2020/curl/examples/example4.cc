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
  sapi::StatusOr<int> status_or_int;
  sapi::StatusOr<CURL*> status_or_curl;
  sapi::StatusOr<CURLM*> status_or_curlm;

  // Initialize sandbox2 and sapi
  CurlSapiSandbox sandbox;
  status = sandbox.Init();
  if (!status.ok()) {
    LOG(FATAL) << "Couldn't initialize Sandboxed API: " << status;
  }
  CurlApi api(&sandbox);

  // Number of running handles
  sapi::v::Int still_running(1);

  // Initialize curl (CURL_GLOBAL_DEFAULT = 3)
  status_or_int = api.curl_global_init(3l);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    LOG(FATAL) << "curl_global_init failed: " << status_or_int.status();
  }

  // Initialize http_handle
  status_or_curl = api.curl_easy_init();
  if (!status_or_curl.ok()) {
    LOG(FATAL) << "curl_easy_init failed: " << status_or_curl.status();
  }
  sapi::v::RemotePtr http_handle(status_or_curl.value());
  if (!http_handle.GetValue()) {
    LOG(FATAL) << "curl_easy_init failed: http_handle is NULL";
  }

  // Specify URL to get
  sapi::v::ConstCStr url("http://example.com");
  status_or_int =
      api.curl_easy_setopt_ptr(&http_handle, CURLOPT_URL, url.PtrBefore());
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_setopt_ptr failed: " << status_or_int.status();
  }

  // Initialize multi_handle
  status_or_curlm = api.curl_multi_init();
  if (!status_or_curlm.ok()) {
    LOG(FATAL) << "curl_multi_init failed: " << status_or_curlm.status();
  }
  sapi::v::RemotePtr multi_handle(status_or_curlm.value());
  if (!multi_handle.GetValue()) {
    LOG(FATAL) << "curl_multi_init failed: multi_handle is NULL";
  }

  // Add http_handle to the multi stack
  status_or_int = api.curl_multi_add_handle(&multi_handle, &http_handle);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    LOG(FATAL) << "curl_multi_add_handle failed: " << status_or_int.status();
  }

  while (still_running.GetValue()) {
    sapi::v::Int numfds(0);

    // Perform the request
    status_or_int =
        api.curl_multi_perform(&multi_handle, still_running.PtrBoth());
    if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
      LOG(FATAL) << "curl_mutli_perform failed: " << status_or_int.status();
    }

    if (still_running.GetValue()) {
      // Wait for an event or timeout
      sapi::v::NullPtr null_ptr;
      status_or_int = api.curl_multi_poll_sapi(&multi_handle, &null_ptr, 0,
                                               1000, numfds.PtrBoth());
      if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
        LOG(FATAL) << "curl_multi_poll_sapi failed: " << status_or_int.status();
      }
    }
  }

  // Remove http_handle from the multi stack
  status_or_int = api.curl_multi_remove_handle(&multi_handle, &http_handle);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    LOG(FATAL) << "curl_multi_remove_handle failed: " << status_or_int.status();
  }

  // Cleanup http_handle
  status = api.curl_easy_cleanup(&http_handle);
  if (!status.ok()) {
    LOG(FATAL) << "curl_easy_cleanup failed: " << status;
  }

  // Cleanup multi_handle
  status_or_int = api.curl_multi_cleanup(&multi_handle);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    LOG(FATAL) << "curl_multi_cleanup failed: " << status_or_int.status();
  }

  // Cleanup curl
  status = api.curl_global_cleanup();
  if (!status.ok()) {
    LOG(FATAL) << "curl_global_cleanup failed: " << status;
  }

  return EXIT_SUCCESS;
}
