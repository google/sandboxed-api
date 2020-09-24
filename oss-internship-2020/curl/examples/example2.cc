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

  // Generate pointer to WriteMemoryCallback function
  void* function_ptr;
  status = sandbox.rpc_channel()->Symbol("WriteToMemory", &function_ptr);
  if (!status.ok()) {
    LOG(FATAL) << "sapi::Sandbox::rpc_channel().Symbol failed: " << status;
  }
  sapi::v::RemotePtr remote_function_ptr(function_ptr);

  // Initialize the curl session
  absl::StatusOr<CURL*> curl_handle = api.curl_easy_init();
  if (!curl_handle.ok()) {
    LOG(FATAL) << "curl_easy_init failed: " << curl_handle.status();
  }
  sapi::v::RemotePtr curl(curl_handle.value());
  if (!curl.GetValue()) {
    LOG(FATAL) << "curl_easy_init failed: curl is NULL";
  }

  absl::StatusOr<int> curl_code;

  // Specify URL to get
  sapi::v::ConstCStr url("http://example.com");
  curl_code = api.curl_easy_setopt_ptr(&curl, CURLOPT_URL, url.PtrBefore());
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_setopt_ptr failed: " << curl_code.status();
  }

  // Set WriteMemoryCallback as the write function
  curl_code = api.curl_easy_setopt_ptr(&curl, CURLOPT_WRITEFUNCTION,
                                       &remote_function_ptr);
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_setopt_ptr failed: " << curl_code.status();
  }

  // Pass 'chunk' struct to the callback function
  sapi::v::LenVal chunk(0);
  curl_code =
      api.curl_easy_setopt_ptr(&curl, CURLOPT_WRITEDATA, chunk.PtrBoth());
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_setopt_ptr failed: " << curl_code.status();
  }

  // Set a user agent
  sapi::v::ConstCStr user_agent("libcurl-agent/1.0");
  curl_code = api.curl_easy_setopt_ptr(&curl, CURLOPT_USERAGENT,
                                       user_agent.PtrBefore());
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_setopt_ptr failed: " << curl_code.status();
  }

  // Perform the request
  curl_code = api.curl_easy_perform(&curl);
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_perform failed: " << curl_code.status();
  }

  // Retrieve memory size
  status = sandbox.TransferFromSandboxee(&chunk);
  if (!status.ok()) {
    LOG(FATAL) << "sandbox.TransferFromSandboxee failed: " << status;
  }
  std::cout << "memory size: " << chunk.GetDataSize() << " bytes" << std::endl;

  // Cleanup curl
  status = api.curl_easy_cleanup(&curl);
  if (!status.ok()) {
    LOG(FATAL) << "curl_easy_cleanup failed: " << status;
  }

  return EXIT_SUCCESS;
}
