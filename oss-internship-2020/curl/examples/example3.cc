// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Sandboxed version of simplessl.c
// HTTPS GET request

#include <cstdlib>

#include "../curl_util.h"    // NOLINT(build/include)
#include "../sandbox.h"      // NOLINT(build/include)
#include "curl_sapi.sapi.h"  // NOLINT(build/include)
#include "absl/strings/str_cat.h"
#include "sandboxed_api/util/status_macros.h"

namespace {

class CurlSapiSandboxEx3 : public curl::CurlSapiSandbox {
 public:
  CurlSapiSandboxEx3(std::string ssl_certificate, std::string ssl_key,
                     std::string ca_certificates)
      : ssl_certificate(std::move(ssl_certificate)),
        ssl_key(std::move(ssl_key)),
        ca_certificates(std::move(ca_certificates)) {}

 private:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    // Add the syscalls and files missing in CurlSandbox to a new PolicyBuilder
    auto policy_builder = std::make_unique<sandbox2::PolicyBuilder>();
    (*policy_builder)
        .AllowGetPIDs()
        .AllowGetRandom()
        .AllowHandleSignals()
        .AddFile(ssl_certificate)
        .AddFile(ssl_key)
        .AddFile(ca_certificates);
    // Provide the new PolicyBuilder to ModifyPolicy in CurlSandbox
    return curl::CurlSapiSandbox::ModifyPolicy(policy_builder.get());
  }

  std::string ssl_certificate;
  std::string ssl_key;
  std::string ca_certificates;
};

absl::Status Example3(const std::string& ssl_certificate,
                      const std::string& ssl_key,
                      const std::string& ssl_key_password,
                      const std::string& ca_certificates) {
  // Initialize sandbox2 and sapi
  CurlSapiSandboxEx3 sandbox(ssl_certificate, ssl_key, ca_certificates);
  SAPI_RETURN_IF_ERROR(sandbox.Init());
  curl::CurlApi api(&sandbox);

  int curl_code;

  // Initialize curl (CURL_GLOBAL_DEFAULT = 3)
  SAPI_ASSIGN_OR_RETURN(curl_code, api.curl_global_init(3l));
  if (curl_code != 0) {
    return absl::UnavailableError(absl::StrCat(
        "curl_global_init failed: ", curl::StrError(&api, curl_code)));
  }

  // Initialize curl easy handle
  curl::CURL* curl_handle;
  SAPI_ASSIGN_OR_RETURN(curl_handle, api.curl_easy_init());
  sapi::v::RemotePtr curl(curl_handle);
  if (!curl_handle) {
    return absl::UnavailableError("curl_easy_init failed: Invalid curl handle");
  }

  // Specify URL to get (using HTTPS)
  sapi::v::ConstCStr url("https://example.com");
  SAPI_ASSIGN_OR_RETURN(
      curl_code,
      api.curl_easy_setopt_ptr(&curl, curl::CURLOPT_URL, url.PtrBefore()));
  if (curl_code != 0) {
    return absl::UnavailableError(absl::StrCat(
        "curl_easy_setopt_ptr failed: ", curl::StrError(&api, curl_code)));
  }

  // Set the SSL certificate type to "PEM"
  sapi::v::ConstCStr ssl_cert_type("PEM");
  SAPI_ASSIGN_OR_RETURN(
      curl_code, api.curl_easy_setopt_ptr(&curl, curl::CURLOPT_SSLCERTTYPE,
                                          ssl_cert_type.PtrBefore()));
  if (curl_code != 0) {
    return absl::UnavailableError(absl::StrCat(
        "curl_easy_setopt_ptr failed: ", curl::StrError(&api, curl_code)));
  }

  // Set the certificate for client authentication
  sapi::v::ConstCStr sapi_ssl_certificate(ssl_certificate.c_str());
  SAPI_ASSIGN_OR_RETURN(
      curl_code, api.curl_easy_setopt_ptr(&curl, curl::CURLOPT_SSLCERT,
                                          sapi_ssl_certificate.PtrBefore()));
  if (curl_code != 0) {
    return absl::UnavailableError(absl::StrCat(
        "curl_easy_setopt_ptr failed: ", curl::StrError(&api, curl_code)));
  }

  // Set the private key for client authentication
  sapi::v::ConstCStr sapi_ssl_key(ssl_key.c_str());
  SAPI_ASSIGN_OR_RETURN(curl_code,
                        api.curl_easy_setopt_ptr(&curl, curl::CURLOPT_SSLKEY,
                                                 sapi_ssl_key.PtrBefore()));
  if (curl_code != 0) {
    return absl::UnavailableError(absl::StrCat(
        "curl_easy_setopt_ptr failed: ", curl::StrError(&api, curl_code)));
  }

  // Set the password used to protect the private key
  sapi::v::ConstCStr sapi_ssl_key_password(ssl_key_password.c_str());
  SAPI_ASSIGN_OR_RETURN(
      curl_code, api.curl_easy_setopt_ptr(&curl, curl::CURLOPT_KEYPASSWD,
                                          sapi_ssl_key_password.PtrBefore()));
  if (curl_code != 0) {
    return absl::UnavailableError(absl::StrCat(
        "curl_easy_setopt_ptr failed: ", curl::StrError(&api, curl_code)));
  }

  // Set the file with the certificates vaildating the server
  sapi::v::ConstCStr sapi_ca_certificates(ca_certificates.c_str());
  SAPI_ASSIGN_OR_RETURN(
      curl_code, api.curl_easy_setopt_ptr(&curl, curl::CURLOPT_CAINFO,
                                          sapi_ca_certificates.PtrBefore()));
  if (curl_code != 0) {
    return absl::UnavailableError(absl::StrCat(
        "curl_easy_setopt_ptr failed: ", curl::StrError(&api, curl_code)));
  }

  // Verify the authenticity of the server
  SAPI_ASSIGN_OR_RETURN(
      curl_code,
      api.curl_easy_setopt_long(&curl, curl::CURLOPT_SSL_VERIFYPEER, 1L));
  if (curl_code != 0) {
    return absl::UnavailableError(absl::StrCat(
        "curl_easy_setopt_long failed: ", curl::StrError(&api, curl_code)));
  }

  // Perform the request
  SAPI_ASSIGN_OR_RETURN(curl_code, api.curl_easy_perform(&curl));
  if (curl_code != 0) {
    return absl::UnavailableError(absl::StrCat(
        "curl_easy_perform failed: ", curl::StrError(&api, curl_code)));
  }

  // Cleanup curl easy handle
  SAPI_RETURN_IF_ERROR(api.curl_easy_cleanup(&curl));

  // Cleanup curl
  SAPI_RETURN_IF_ERROR(api.curl_global_cleanup());

  return absl::OkStatus();
}

}  // namespace

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  sapi::InitLogging(argv[0]);

  // Get input parameters (should be absolute paths)
  if (argc != 5) {
    LOG(ERROR) << "wrong number of arguments (4 expected)";
    return EXIT_FAILURE;
  }

  if (absl::Status status = Example3(argv[1], argv[2], argv[3], argv[4]);
      !status.ok()) {
    LOG(ERROR) << "Example3 failed: " << status.ToString();
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
