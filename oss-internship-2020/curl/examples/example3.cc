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

#include "../sandbox.h"

class CurlSapiSandboxEx3 : public CurlSapiSandbox {
 public:
  CurlSapiSandboxEx3(std::string ssl_certificate, std::string ssl_key,
                     std::string ca_certificates)
      : ssl_certificate(ssl_certificate),
        ssl_key(ssl_key),
        ca_certificates(ca_certificates) {}

 private:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    // Add the syscalls and files missing in CurlSandbox to a new PolicyBuilder
    auto policy_builder = std::make_unique<sandbox2::PolicyBuilder>();
    (*policy_builder)
        .AllowGetPIDs()
        .AllowGetRandom()
        .AllowSyscall(__NR_sysinfo)
        .AddFile(ssl_certificate)
        .AddFile(ssl_key)
        .AddFile(ca_certificates);
    // Provide the new PolicyBuilder to ModifyPolicy in CurlSandbox
    return CurlSapiSandbox::ModifyPolicy(policy_builder.get());
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
  if (argc != 5) {
    std::cerr << "wrong number of arguments (4 expected)" << std::endl;
    return EXIT_FAILURE;
  }
  std::string ssl_certificate = argv[1];
  std::string ssl_key = argv[2];
  std::string ssl_key_password = argv[3];
  std::string ca_certificates = argv[4];

  // Initialize sandbox2 and sapi
  CurlSapiSandboxEx3 sandbox(ssl_certificate, ssl_key, ca_certificates);
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

  // Initialize curl easy handle
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

  // Specify URL to get (using HTTPS)
  sapi::v::ConstCStr url("https://example.com");
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_URL, url.PtrBefore());
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_easy_setopt_ptr" << std::endl;
    return EXIT_FAILURE;
  }

  // Set the SSL certificate type to "PEM"
  sapi::v::ConstCStr ssl_cert_type("PEM");
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_SSLCERTTYPE,
                                           ssl_cert_type.PtrBefore());
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_easy_setopt_ptr" << std::endl;
    return EXIT_FAILURE;
  }

  // Set the certificate for client authentication
  sapi::v::ConstCStr sapi_ssl_certificate(ssl_certificate.c_str());
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_SSLCERT,
                                           sapi_ssl_certificate.PtrBefore());
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_easy_setopt_ptr" << std::endl;
    return EXIT_FAILURE;
  }

  // Set the private key for client authentication
  sapi::v::ConstCStr sapi_ssl_key(ssl_key.c_str());
  status_or_int =
      api.curl_easy_setopt_ptr(&curl, CURLOPT_SSLKEY, sapi_ssl_key.PtrBefore());
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_easy_setopt_ptr" << std::endl;
    return EXIT_FAILURE;
  }

  // Set the password used to protect the private key
  sapi::v::ConstCStr sapi_ssl_key_password(ssl_key_password.c_str());
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_KEYPASSWD,
                                           sapi_ssl_key_password.PtrBefore());
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_easy_setopt_ptr" << std::endl;
    return EXIT_FAILURE;
  }

  // Set the file with the certificates vaildating the server
  sapi::v::ConstCStr sapi_ca_certificates(ca_certificates.c_str());
  status_or_int = api.curl_easy_setopt_ptr(&curl, CURLOPT_CAINFO,
                                           sapi_ca_certificates.PtrBefore());
  if (!status_or_int.ok() or status_or_int.value() != CURLE_OK) {
    std::cerr << "error in curl_easy_setopt_ptr" << std::endl;
    return EXIT_FAILURE;
  }

  // Verify the authenticity of the server
  status_or_int = api.curl_easy_setopt_long(&curl, CURLOPT_SSL_VERIFYPEER, 1L);
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

  // Cleanup curl easy handle
  status = api.curl_easy_cleanup(&curl);
  if (!status.ok()) {
    std::cerr << "error in curl_easy_cleanup" << std::endl;
    return EXIT_FAILURE;
  }

  // Cleanup curl
  status = api.curl_global_cleanup();
  if (!status.ok()) {
    std::cerr << "error in curl_global_cleanup" << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
