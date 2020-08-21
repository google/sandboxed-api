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
  if (!status_or_curl.ok())
    LOG(FATAL) << "curl_easy_init failed: " << status_or_curl.status();
  sapi::v::RemotePtr curl(status_or_curl.value());
  if (!curl.GetValue()) LOG(FATAL) << "curl_easy_init failed: curl is NULL";

  // Specify URL to get
  sapi::v::ConstCStr sapi_url(((thread_args*)args)->url);
  status_or_int =
      api.curl_easy_setopt_ptr(&curl, CURLOPT_URL, sapi_url.PtrBefore());
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK)
    LOG(FATAL) << "curl_easy_setopt_ptr failed: " << status_or_int.status();

  // Perform the request
  status_or_int = api.curl_easy_perform(&curl);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK)
    LOG(FATAL) << "curl_easy_perform failed: " << status_or_int.status();

  // Cleanup curl
  status = api.curl_easy_cleanup(&curl);
  if (!status.ok()) LOG(FATAL) << "curl_easy_cleanup failed: " << status;

  return NULL;
}

const char* const urls[kThreadsnumber] = {
    "http://example.com", "http://example.edu", "http://example.net",
    "http://example.org"};

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  pthread_t tid[kThreadsnumber];

  absl::Status status;
  sapi::StatusOr<int> status_or_int;

  // Initialize sandbox2 and sapi
  CurlSapiSandbox sandbox;
  status = sandbox.Init();
  if (!status.ok())
    LOG(FATAL) << "Couldn't initialize Sandboxed API: " << status;
  CurlApi api(&sandbox);

  // Initialize curl (CURL_GLOBAL_DEFAULT = 3)
  status_or_int = api.curl_global_init(3l);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK)
    LOG(FATAL) << "curl_global_init failed: " << status_or_int.status();

  // Create the threads
  for (int i = 0; i < kThreadsnumber; ++i) {
    thread_args args = {urls[i], &api};
    int error = pthread_create(&tid[i], NULL, pull_one_url, (void*)&args);
    if (error) LOG(FATAL) << "pthread_create failed";
    std::cout << "Thread " << i << " gets " << urls[i] << std::endl;
  }

  // Join the threads
  for (int i = 0; i < kThreadsnumber; ++i) {
    int error = pthread_join(tid[i], NULL);
    if (error) LOG(FATAL) << "pthread_join failed";
    std::cout << "Thread " << i << " terminated" << std::endl;
  }

  // Cleanup curl
  status = api.curl_global_cleanup();
  if (!status.ok()) LOG(FATAL) << "curl_global_cleanup failed: " << status;

  return EXIT_SUCCESS;
}
