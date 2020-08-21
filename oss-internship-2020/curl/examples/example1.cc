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

#include "../sandbox.h"

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  absl::Status status;
  sapi::StatusOr<CURL*> status_or_curl;
  sapi::StatusOr<int> status_or_int;

  // Initialize sandbox2 and sapi
  CurlSapiSandbox sandbox;
  status = sandbox.Init();
  if (!status.ok())
    LOG(FATAL) << "Couldn't initialize Sandboxed API: " << status;
  CurlApi api(&sandbox);

  // Initialize the curl session
  status_or_curl = api.curl_easy_init();
  if (!status_or_curl.ok())
    LOG(FATAL) << "curl_easy_init failed: " << status_or_curl.status();
  sapi::v::RemotePtr curl(status_or_curl.value());
  if (!curl.GetValue()) LOG(FATAL) << "curl_easy_init failed: curl is NULL";

  // Specify URL to get
  sapi::v::ConstCStr url("http://example.com");
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_URL, url.PtrBefore());
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK)
    LOG(FATAL) << "curl_easy_setopt_ptr failed: " << status_or_int.status();

  // Set the library to follow a redirection
  status_or_int = api.curl_easy_setopt_long(&curl, CURLOPT_FOLLOWLOCATION, 1l);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK)
    LOG(FATAL) << "curl_easy_setopt_long failed: " << status_or_int.status();

  // Disable authentication of peer certificate
  status_or_int = api.curl_easy_setopt_long(&curl, CURLOPT_SSL_VERIFYPEER, 0l);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK)
    LOG(FATAL) << "curl_easy_setopt_long failed: " << status_or_int.status();

  // Perform the request
  status_or_int = api.curl_easy_perform(&curl);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK)
    LOG(FATAL) << "curl_easy_perform failed: " << status_or_int.status();

  // Cleanup curl
  status = api.curl_easy_cleanup(&curl);
  if (!status.ok()) LOG(FATAL) << "curl_easy_cleanup failed: " << status;

  return EXIT_SUCCESS;
}
