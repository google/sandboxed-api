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

int main() {
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

  // Generate pointer to WriteMemoryCallback function
  sapi::RPCChannel rpcc(sandbox.comms());
  size_t (*_function_ptr)(void*, size_t, size_t, void*);
  status = rpcc.Symbol("WriteMemoryCallback", (void**)&_function_ptr);
  if (!status.ok()) {
    std::cerr << "error in RPCChannel Symbol" << std::endl;
    return EXIT_FAILURE;
  }
  sapi::v::RemotePtr remote_function_ptr((void*)_function_ptr);

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

  // Set WriteMemoryCallback as the write function
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_WRITEFUNCTION,
                                           &remote_function_ptr);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_easy_setopt_ptr" << std::endl;
    return EXIT_FAILURE;
  }

  // Pass 'chunk' struct to the callback function
  sapi::v::Struct<MemoryStruct> chunk;
  status_or_int =
      api.curl_easy_setopt_ptr(&curl, CURLOPT_WRITEDATA, chunk.PtrBoth());
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_easy_setopt_ptr" << std::endl;
    return EXIT_FAILURE;
  }

  // Set a user agent
  sapi::v::ConstCStr user_agent("libcurl-agent/1.0");
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_USERAGENT,
                                           user_agent.PtrBefore());
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_easy_setopt_ptr" << std::endl;
    return EXIT_FAILURE;
  }

  // Perform the request
  status_or_int = api.curl_easy_perform(&curl);
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_easy_perform" << std::endl;
    return EXIT_FAILURE;
  }

  // Retrieve memory size
  sapi::v::Int size;
  size.SetRemote(&((MemoryStruct*)chunk.GetRemote())->size);
  status = sandbox.TransferFromSandboxee(&size);
  if (!status.ok()) {
    std::cerr << "error in sandbox TransferFromSandboxee" << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "memory size: " << size.GetValue() << " bytes" << std::endl;

  // Cleanup curl
  status = api.curl_easy_cleanup(&curl);
  if (!status.ok()) {
    std::cerr << "error in curl_easy_cleanup" << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
