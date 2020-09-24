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

#include "../sandbox.h"  // NOLINT(build/include)

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
        .AllowFutexOp(FUTEX_WAIT_PRIVATE)
        .AllowGetPIDs()
        .AllowGetRandom()
        .AllowHandleSignals()
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
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  absl::Status status;

  // Get input parameters (should be absolute paths)
  if (argc != 5) {
    LOG(FATAL) << "wrong number of arguments (4 expected)";
  }
  std::string ssl_certificate = argv[1];
  std::string ssl_key = argv[2];
  std::string ssl_key_password = argv[3];
  std::string ca_certificates = argv[4];

  // Initialize sandbox2 and sapi
  CurlSapiSandboxEx3 sandbox(ssl_certificate, ssl_key, ca_certificates);
  status = sandbox.Init();
  if (!status.ok()) {
    LOG(FATAL) << "Couldn't initialize Sandboxed API: " << status;
  }
  CurlApi api(&sandbox);

  absl::StatusOr<int> curl_code;

  // Initialize curl (CURL_GLOBAL_DEFAULT = 3)
  curl_code = api.curl_global_init(3l);
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_global_init failed: " << curl_code.status();
  }

  // Initialize curl easy handle
  absl::StatusOr<CURL*> curl_handle = api.curl_easy_init();
  if (!curl_handle.ok()) {
    LOG(FATAL) << "curl_easy_init failed: " << curl_handle.status();
  }
  sapi::v::RemotePtr curl(curl_handle.value());
  if (!curl.GetValue()) {
    LOG(FATAL) << "curl_easy_init failed: curl is NULL";
  }

  // Specify URL to get (using HTTPS)
  sapi::v::ConstCStr url("https://example.com");
  curl_code = api.curl_easy_setopt_ptr(&curl, CURLOPT_URL, url.PtrBefore());
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_setopt_ptr failed: " << curl_code.status();
  }

  // Set the SSL certificate type to "PEM"
  sapi::v::ConstCStr ssl_cert_type("PEM");
  curl_code = api.curl_easy_setopt_ptr(&curl, CURLOPT_SSLCERTTYPE,
                                       ssl_cert_type.PtrBefore());
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_setopt_ptr failed: " << curl_code.status();
  }

  // Set the certificate for client authentication
  sapi::v::ConstCStr sapi_ssl_certificate(ssl_certificate.c_str());
  curl_code = api.curl_easy_setopt_ptr(&curl, CURLOPT_SSLCERT,
                                       sapi_ssl_certificate.PtrBefore());
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_setopt_ptr failed: " << curl_code.status();
  }

  // Set the private key for client authentication
  sapi::v::ConstCStr sapi_ssl_key(ssl_key.c_str());
  curl_code =
      api.curl_easy_setopt_ptr(&curl, CURLOPT_SSLKEY, sapi_ssl_key.PtrBefore());
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_setopt_ptr failed: " << curl_code.status();
  }

  // Set the password used to protect the private key
  sapi::v::ConstCStr sapi_ssl_key_password(ssl_key_password.c_str());
  curl_code = api.curl_easy_setopt_ptr(&curl, CURLOPT_KEYPASSWD,
                                       sapi_ssl_key_password.PtrBefore());
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_setopt_ptr failed: " << curl_code.status();
  }

  // Set the file with the certificates vaildating the server
  sapi::v::ConstCStr sapi_ca_certificates(ca_certificates.c_str());
  curl_code = api.curl_easy_setopt_ptr(&curl, CURLOPT_CAINFO,
                                       sapi_ca_certificates.PtrBefore());
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_setopt_ptr failed: " << curl_code.status();
  }

  // Verify the authenticity of the server
  curl_code = api.curl_easy_setopt_long(&curl, CURLOPT_SSL_VERIFYPEER, 1L);
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_setopt_long failed: " << curl_code.status();
  }

  // Perform the request
  curl_code = api.curl_easy_perform(&curl);
  if (!curl_code.ok() || curl_code.value() != CURLE_OK) {
    LOG(FATAL) << "curl_easy_perform failed: " << curl_code.status();
  }

  // Cleanup curl easy handle
  status = api.curl_easy_cleanup(&curl);
  if (!status.ok()) {
    LOG(FATAL) << "curl_easy_cleanup failed: " << status;
  }

  // Cleanup curl
  status = api.curl_global_cleanup();
  if (!status.ok()) {
    LOG(FATAL) << "curl_global_cleanup failed: " << status;
  }

  return EXIT_SUCCESS;
}
