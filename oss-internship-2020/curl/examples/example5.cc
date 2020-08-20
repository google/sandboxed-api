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

// Sandboxed version of multithread.c
// Multithreaded HTTP GET requests

#include <pthread.h>

#include <cstdlib>

#include "../sandbox.h"

struct thread_args {
  const char* url;
  CurlApi* api;
};

constexpr int kThreadsnumber = 4;

void* pull_one_url(void* args) {
  absl::Status status;
  sapi::StatusOr<CURL*> status_or_curl;
  sapi::StatusOr<int> status_or_int;

  CurlApi& api = *((thread_args*)args)->api;

  // Initialize the curl session
  status_or_curl = api.curl_easy_init();
  if (!status_or_curl.ok()) {
    std::cerr << "error in curl_easy_init" << std::endl;
    return NULL;
  }
  sapi::v::RemotePtr curl(status_or_curl.value());
  if (!curl.GetValue()) {
    std::cerr << "error in curl_easy_init" << std::endl;
    return NULL;
  }

  // Specify URL to get
  sapi::v::ConstCStr sapi_url(((thread_args*)args)->url);
  status_or_int =
      api.curl_easy_setopt_ptr(&curl, CURLOPT_URL, sapi_url.PtrBefore());
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_easy_setopt_ptr" << std::endl;
    return NULL;
  }

  // Perform the request
  status_or_int = api.curl_easy_perform(&curl);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_easy_perform" << std::endl;
    return NULL;
  }

  // Cleanup curl
  status = api.curl_easy_cleanup(&curl);
  if (!status.ok()) {
    std::cerr << "error in curl_easy_cleanup" << std::endl;
    return NULL;
  }

  return NULL;
}

const char* const urls[kThreadsnumber] = {
    "http://example.com", "http://example.edu", "http://example.net",
    "http://example.org"};

int main(int argc, char** argv) {
  pthread_t tid[kThreadsnumber];

  absl::Status status;
  sapi::StatusOr<int> status_or_int;

  // Initialize sandbox2 and sapi
  CurlSapiSandbox sandbox;
  status = sandbox.Init();
  if (!status.ok()) {
    std::cerr << "error in sandbox Init" << std::endl;
    return EXIT_FAILURE;
  }
  CurlApi api(&sandbox);

  // Initialize curl (CURL_GLOBAL_DEFAULT = 3)
  status_or_int = api.curl_global_init(3l);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_global_init" << std::endl;
    return EXIT_FAILURE;
  }

  // Create the threads
  for (int i = 0; i < kThreadsnumber; ++i) {
    thread_args args = {urls[i], &api};
    int error = pthread_create(&tid[i], NULL, pull_one_url, (void*)&args);
    if (error) {
      std::cerr << "error in pthread_create" << std::endl;
      return EXIT_FAILURE;
    }
    std::cout << "Thread " << i << " gets " << urls[i] << std::endl;
  }

  // Join the threads
  for (int i = 0; i < kThreadsnumber; i++) {
    pthread_join(tid[i], NULL);
    std::cout << "Thread " << i << " terminated" << std::endl;
  }

  // Cleanup curl
  status = api.curl_global_cleanup();
  if (!status.ok()) {
    std::cerr << "error in curl_global_cleanup" << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
