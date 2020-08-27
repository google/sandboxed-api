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

// Sandboxed version of getinmemory.c
// HTTP GET request using callbacks

#include <cstdlib>
#include <iostream>

#include "../sandbox.h"

struct MemoryStruct {
  char* memory;
  size_t size;
};

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  absl::Status status;
  sapi::StatusOr<CURL*> status_or_curl;
  sapi::StatusOr<int> status_or_int;

  // Initialize sandbox2 and sapi
  CurlSapiSandbox sandbox;
  status = sandbox.Init();
  if (!status.ok()) {
    LOG(FATAL) << "Couldn't initialize Sandboxed API: " << status;
  }
  CurlApi api(&sandbox);

  // Generate pointer to WriteMemoryCallback function
  sapi::RPCChannel rpcc(sandbox.comms());
  size_t (*_function_ptr)(void*, size_t, size_t, void*);
  status = rpcc.Symbol("WriteMemoryCallback", (void**)&_function_ptr);
  if (!status.ok()) {
    LOG(FATAL) << "rpcc.Symbol failed: " << status;
  }
  sapi::v::RemotePtr remote_function_ptr((void*)_function_ptr);

  // Initialize the curl session
  status_or_curl = api.curl_easy_init();
  if (!status_or_curl.ok()) {
    LOG(FATAL) << "curl_easy_init failed: " << status_or_curl.status();
  }
  sapi::v::RemotePtr curl(status_or_curl.value());
  if (!curl.GetValue()) {
    LOG(FATAL) << "curl_easy_init failed: curl is NULL";
  }

  // Specify URL to get
  sapi::v::ConstCStr url("http://example.com");
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_URL, url.PtrBefore());
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_setopt_ptr failed: " << status_or_int.status();
  }

  // Set WriteMemoryCallback as the write function
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_WRITEFUNCTION,
                                           &remote_function_ptr);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_setopt_ptr failed: " << status_or_int.status();
  }

  // Pass 'chunk' struct to the callback function
  sapi::v::Struct<MemoryStruct> chunk;
  status_or_int =
      api.curl_easy_setopt_ptr(&curl, CURLOPT_WRITEDATA, chunk.PtrBoth());
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_setopt_ptr failed: " << status_or_int.status();
  }

  // Set a user agent
  sapi::v::ConstCStr user_agent("libcurl-agent/1.0");
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_USERAGENT,
                                           user_agent.PtrBefore());
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_setopt_ptr failed: " << status_or_int.status();
  }

  // Perform the request
  status_or_int = api.curl_easy_perform(&curl);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_perform failed: " << status_or_int.status();
  }

  // Retrieve memory size
  sapi::v::Int size;
  size.SetRemote(&((MemoryStruct*)chunk.GetRemote())->size);
  status = sandbox.TransferFromSandboxee(&size);
  if (!status.ok()) {
    LOG(FATAL) << "sandbox.TransferFromSandboxee failed: " << status;
  }
  std::cout << "memory size: " << size.GetValue() << " bytes" << std::endl;

  // Cleanup curl
  status = api.curl_easy_cleanup(&curl);
  if (!status.ok()) {
    LOG(FATAL) << "curl_easy_cleanup failed: " << status;
  }

  return EXIT_SUCCESS;
}
