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

#include <iostream>

#include "curl_sapi.sapi.h"

class CurlApiSandboxEx1 : public CurlSandbox {

  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {

    return sandbox2::PolicyBuilder()
        .DangerDefaultAllowAll()
        .AllowUnrestrictedNetworking()
        .AddDirectory("/lib")
        .BuildOrDie();
  }

};

int main(int argc, char* argv[]) {

  // GET http://example.com

  CurlApiSandboxEx1 sandbox;
  
  auto status_sandbox_init = sandbox.Init();
  assert(status_sandbox_init.ok());

  CurlApi api(&sandbox);

  auto status_init = api.curl_easy_init();
  assert(status_init.ok());

  ::sapi::v::RemotePtr curl(status_init.value());
  assert(curl.GetValue());

  auto status_setopt_verbose = api.curl_easy_setopt_long(&curl, CURLOPT_VERBOSE, 1l);
  assert(status_setopt_verbose.ok());
  assert(status_setopt_verbose.value() == CURLE_OK);

  sapi::v::ConstCStr url("http://example.com");
  
  auto status_setopt_url = api.curl_easy_setopt_ptr(&curl, CURLOPT_URL, url.PtrBefore());
  assert(status_setopt_url.ok());
  assert(status_setopt_url.value() == CURLE_OK);

  auto status_perform = api.curl_easy_perform(&curl);
  assert(status_perform.ok());
  assert(status_perform.value() == CURLE_OK);

  auto status_cleanup = api.curl_easy_cleanup(&curl);
  assert(status_cleanup.ok());

}
