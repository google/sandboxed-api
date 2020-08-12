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
#include <iostream>

#include "curl_sapi.sapi.h"
#include "sandboxed_api/util/flag.h"

class CurlApiSandboxEx4 : public CurlSandbox {
  private:
    std::unique_ptr<sandbox2::Policy> ModifyPolicy( 
        sandbox2::PolicyBuilder*) override {
      // Return a new policy
      return sandbox2::PolicyBuilder()
          .DangerDefaultAllowAll()
          .AllowUnrestrictedNetworking()
          .AddDirectory("/lib")
          .BuildOrDie();
    }
};

int main() {

  absl::Status status;
  sapi::StatusOr<int> status_or_int;
  sapi::StatusOr<CURL*> status_or_curl;
  sapi::StatusOr<CURLM*> status_or_curlm;

  // Initialize sandbox2 and sapi
  CurlApiSandboxEx4 sandbox;
  status = sandbox.Init();
  assert(status.ok());
  CurlApi api(&sandbox);

  // Number of running handles
  sapi::v::Int still_running(1);

  // Initialize curl (CURL_GLOBAL_DEFAULT = 3)
  status_or_int = api.curl_global_init(3l);
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  // Initialize http_handle
  status_or_curl = api.curl_easy_init();
  assert(status_or_curl.ok());
  sapi::v::RemotePtr http_handle(status_or_curl.value());
  assert(http_handle.GetValue());  // Checking http_handle != nullptr
  
  // Specify URL to get
  sapi::v::ConstCStr url("http://example.com");
  status_or_int = 
    api.curl_easy_setopt_ptr(&http_handle, CURLOPT_URL, url.PtrBefore());
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  // Initialize multi_handle
  status_or_curlm = api.curl_multi_init();
  assert(status_or_curlm.ok());
  sapi::v::RemotePtr multi_handle(status_or_curlm.value());
  assert(multi_handle.GetValue());  // Checking multi_handle != nullptr

  // Add http_handle to the multi stack
  status_or_int = api.curl_multi_add_handle(&multi_handle, &http_handle);
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  while (still_running.GetValue()) { 

    sapi::v::Int numfds(0);
 
    // Perform the request
    status_or_int = api.curl_multi_perform(&multi_handle, 
                                           still_running.PtrBoth());
    assert(status_or_int.ok());
    assert(status_or_int.value() == CURLE_OK);

    if (still_running.GetValue()) {
      // Wait for an event or timeout
      sapi::v::NullPtr null_ptr;
      status_or_int = api.curl_multi_poll_sapi(&multi_handle, &null_ptr, 0, 
                                               1000, numfds.PtrBoth());
      assert(status_or_int.ok());
      assert(status_or_int.value() == CURLM_OK);
    }
  }

  // Remove http_handle from the multi stack
  status_or_int = api.curl_multi_remove_handle(&multi_handle, &http_handle);
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  // Cleanup http_handle
  status = api.curl_easy_cleanup(&http_handle);
  assert(status.ok());

  // Cleanup multi_handle
  status_or_int = api.curl_multi_cleanup(&multi_handle);
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  // Cleanup curl
  status = api.curl_global_cleanup();
  assert(status.ok());
 
  return EXIT_SUCCESS;
  
}
