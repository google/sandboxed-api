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
  absl::Status status;
  sapi::StatusOr<CURL*> status_or_curl;
  sapi::StatusOr<int> status_or_int;

  // Initialize sandbox2 and sapi
  CurlSapiSandbox sandbox;
  status = sandbox.Init();
  if (!status.ok()) {
    std::cerr << "error in sandbox Init" << std::endl;
    return EXIT_FAILURE;
  }
  CurlApi api(&sandbox);

  // Initialize the curl session
  status_or_curl = api.curl_easy_init();
  if (!status_or_curl.ok()) {
    std::cerr << "error in curl_easy_init" << std::endl;
    return EXIT_FAILURE;
  }
  sapi::v::RemotePtr curl(status_or_curl.value());
  if (!curl.GetValue()) {
    std::cerr << "error in curl_easy_init" << std::endl;
    return EXIT_FAILURE;
  }

  // Specify URL to get
  sapi::v::ConstCStr url("http://example.com");
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_URL, url.PtrBefore());
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_easy_setopt_ptr" << std::endl;
    return EXIT_FAILURE;
  }

  // Set the library to follow a redirection
  status_or_int = api.curl_easy_setopt_long(&curl, CURLOPT_FOLLOWLOCATION, 1l);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_easy_setopt_long" << std::endl;
    return EXIT_FAILURE;
  }

  // curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);
  status_or_int = api.curl_easy_setopt_long(&curl, CURLOPT_SSL_VERIFYPEER, 0l);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_easy_setopt_long" << std::endl;
    return EXIT_FAILURE;
  }

  // Perform the request
  status_or_int = api.curl_easy_perform(&curl);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_easy_perform" << std::endl;
    return EXIT_FAILURE;
  }

  // Cleanup curl
  status = api.curl_easy_cleanup(&curl);
  if (!status.ok()) {
    std::cerr << "error in curl_easy_cleanup" << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
