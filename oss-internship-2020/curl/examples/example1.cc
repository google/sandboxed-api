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

// Sandboxed version of simple.c
// Simple HTTP GET request

#include <cstdlib>

#include "../sandbox.h"  // NOLINT(build/include)

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

  // Initialize the curl session
  absl::StatusOr<CURL*> curl_handle = api.curl_easy_init();
  if (!curl_handle.ok()) {
    LOG(FATAL) << "curl_easy_init failed: " << curl_handle.status();
  }
  sapi::v::RemotePtr curl(curl_handle.value());
  if (!curl.GetValue()) LOG(FATAL) << "curl_easy_init failed: curl is NULL";

  absl::StatusOr<int> curl_code;

  // Specify URL to get
  sapi::v::ConstCStr url("http://example.com");
  curl_code = api.curl_easy_setopt_ptr(&curl, CURLOPT_URL, url.PtrBefore());
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_setopt_ptr failed: " << curl_code.status();
  }

  // Set the library to follow a redirection
  curl_code = api.curl_easy_setopt_long(&curl, CURLOPT_FOLLOWLOCATION, 1l);
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_setopt_long failed: " << curl_code.status();
  }

  // Disable authentication of peer certificate
  curl_code = api.curl_easy_setopt_long(&curl, CURLOPT_SSL_VERIFYPEER, 0l);
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_setopt_long failed: " << curl_code.status();
  }

  // Perform the request
  curl_code = api.curl_easy_perform(&curl);
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_perform failed: " << curl_code.status();
  }

  // Cleanup curl
  status = api.curl_easy_cleanup(&curl);
  if (!status.ok()) {
    LOG(FATAL) << "curl_easy_cleanup failed: " << status;
  }

  return EXIT_SUCCESS;
}
