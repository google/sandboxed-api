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

#include <cstdlib>
#include <iostream>

#include "curl_sapi.sapi.h"
#include "sandboxed_api/util/flag.h"

class CurlApiSandboxEx1 : public CurlSandbox {
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

int main(int argc, char* argv[]) {

  CurlApiSandboxEx1 sandbox; 
  absl::Status status = sandbox.Init();
  assert(status.ok());
  CurlApi api(&sandbox);

  sapi::StatusOr<CURL*> status_or_curl = api.curl_easy_init();
  assert(status_or_curl.ok());

  sapi::v::RemotePtr curl(status_or_curl.value());
  assert(curl.GetValue());  // Checking curl != nullptr

  sapi::v::ConstCStr url("http://example.com");  
  sapi::StatusOr<int> status_or_int = 
    api.curl_easy_setopt_ptr(&curl, CURLOPT_URL, url.PtrBefore());
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  status_or_int = api.curl_easy_setopt_long(&curl, CURLOPT_FOLLOWLOCATION, 1l);
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  status_or_int = api.curl_easy_perform(&curl);
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  status = api.curl_easy_cleanup(&curl);
  assert(status.ok());

  return EXIT_SUCCESS;

}
