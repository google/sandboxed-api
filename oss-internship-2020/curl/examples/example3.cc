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

// Sandboxed version of simplessl.c
// HTTPS GET request

#include <cstdlib>
#include <iostream>

#include "curl_sapi.sapi.h"
#include "sandboxed_api/util/flag.h"

class CurlApiSandboxEx3 : public CurlSandbox {

  public:

    CurlApiSandboxEx3(std::string ssl_certificate, std::string ssl_key, 
                      std::string ca_certificates)
        : ssl_certificate(ssl_certificate), 
          ssl_key(ssl_key),
          ca_certificates(ca_certificates) {}

  private:

    std::unique_ptr<sandbox2::Policy> ModifyPolicy( 
        sandbox2::PolicyBuilder*) override {
      // Return a new policy
      return sandbox2::PolicyBuilder()
          .DangerDefaultAllowAll()
          .AllowUnrestrictedNetworking()
          .AddDirectory("/lib")
          .AddFile(ssl_certificate)
          .AddFile(ssl_key)
          .AddFile(ca_certificates)
          .BuildOrDie();
    }

    std::string ssl_certificate;
    std::string ssl_key;
    std::string ca_certificates;
};


int main(int argc, char* argv[]) {

  absl::Status status;
  sapi::StatusOr<int> status_or_int;
  sapi::StatusOr<CURL*> status_or_curl;

  // Get input parameters (should be absolute paths)
  assert(argc == 5);
  std::string ssl_certificate = argv[1];
  std::string ssl_key = argv[2];
  std::string ssl_key_password = argv[3];
  std::string ca_certificates = argv[4];

  // Initialize sandbox2 and sapi
  CurlApiSandboxEx3 sandbox(ssl_certificate, ssl_key, ca_certificates);
  status = sandbox.Init();
  assert(status.ok());
  CurlApi api(&sandbox);
 
  // Initialize curl (CURL_GLOBAL_DEFAULT = 3)
  status_or_int = api.curl_global_init(3l);
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  // Initialize curl easy handle
  status_or_curl = api.curl_easy_init();
  assert(status_or_curl.ok());
  sapi::v::RemotePtr curl(status_or_curl.value());
  assert(curl.GetValue());  // Checking curl != nullptr

  // Specify URL to get (using HTTPS)
  sapi::v::ConstCStr url("https://example.com");
  status_or_int = 
    api.curl_easy_setopt_ptr(&curl, CURLOPT_URL, url.PtrBefore());
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  // Set the SSL certificate type to "PEM"
  sapi::v::ConstCStr ssl_cert_type("PEM");
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_SSLCERTTYPE, 
                                           ssl_cert_type.PtrBefore());
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  // Set the certificate for client authentication
  sapi::v::ConstCStr sapi_ssl_certificate(ssl_certificate.c_str());
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_SSLCERT, 
                                           sapi_ssl_certificate.PtrBefore());
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  // Set the private key for client authentication
  sapi::v::ConstCStr sapi_ssl_key(ssl_key.c_str());
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_SSLKEY, 
                                           sapi_ssl_key.PtrBefore());
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  // Set the password used to protect the private key
  sapi::v::ConstCStr sapi_ssl_key_password(ssl_key_password.c_str());
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_KEYPASSWD,
                                           sapi_ssl_key_password.PtrBefore());
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  // Set the file with the certificates vaildating the server
  sapi::v::ConstCStr sapi_ca_certificates(ca_certificates.c_str());
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_CAINFO, 
                                           sapi_ca_certificates.PtrBefore());
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  // Verify the authenticity of the server
  status_or_int = api.curl_easy_setopt_long(&curl, CURLOPT_SSL_VERIFYPEER, 1L);
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  // Perform the request
  status_or_int = api.curl_easy_perform(&curl);
  assert(status_or_int.ok());
  assert(status_or_int.value() == CURLE_OK);

  // Cleanup curl easy handle
  status = api.curl_easy_cleanup(&curl);
  assert(status.ok());

  // Cleanup curl
  status = api.curl_global_cleanup();
  assert(status.ok());
 
  return EXIT_SUCCESS;

}