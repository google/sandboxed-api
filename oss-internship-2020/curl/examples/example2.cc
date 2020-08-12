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

#include "curl_sapi.sapi.h"
#include "sandboxed_api/util/flag.h"

struct MemoryStruct {
  char* memory;
  size_t size;
};

class CurlApiSandboxEx2 : public CurlSandbox {
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
  sapi::StatusOr<CURL*> status_or_curl;
  sapi::StatusOr<int> status_or_int;

  // Initialize sandbox2 and sapi
  CurlApiSandboxEx2 sandbox;
  status = sandbox.Init();
  assert(status.ok());
  CurlApi api(&sandbox);

  // Generate pointer to WriteMemoryCallback function
  sapi::RPCChannel rpcc(sandbox.comms());
  size_t (*_function_ptr)(void*, size_t, size_t, void*);
  status = rpcc.Symbol("WriteMemoryCallback", (void**)&_function_ptr);
  assert(status.ok());
  sapi::v::RemotePtr remote_function_ptr((void*)_function_ptr);

  // Initialize the curl session
  status_or_curl = api.curl_easy_init();
  assert(status_or_curl.ok());
  sapi::v::RemotePtr curl(status_or_curl.value());
  assert(curl.GetValue());  // Checking curl != nullptr

  // Specify URL to get
  sapi::v::ConstCStr url("http://example.com");
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_URL, url.PtrBefore());
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  // Set WriteMemoryCallback as the write function
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_WRITEFUNCTION, 
                                           &remote_function_ptr);
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);
  
  // Pass 'chunk' struct to the callback function
  sapi::v::Struct<MemoryStruct> chunk;
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_WRITEDATA, 
                                           chunk.PtrBoth());
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  // Set a user agent
  sapi::v::ConstCStr user_agent("libcurl-agent/1.0");
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_USERAGENT, 
                                           user_agent.PtrBefore());
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  // Perform the request
  status_or_int = api.curl_easy_perform(&curl);
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  // Retrieve memory size
  sapi::v::Int size;
  size.SetRemote(&((MemoryStruct*)chunk.GetRemote())->size);
  status = sandbox.TransferFromSandboxee(&size);
  assert(status.ok());
  std::cout << "memory size: " << size.GetValue() << " bytes" << std::endl;

  // Cleanup curl
  status = api.curl_easy_cleanup(&curl);
  assert(status.ok());

  return EXIT_SUCCESS;

}
