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

// Sandboxed version of simple.c using transactions
// Simple HTTP GET request

#include <cstdlib>

#include "../curl_util.h"    // NOLINT(build/include)
#include "../sandbox.h"      // NOLINT(build/include)
#include "curl_sapi.sapi.h"  // NOLINT(build/include)
#include "absl/strings/str_cat.h"
#include "sandboxed_api/transaction.h"
#include "sandboxed_api/util/status_macros.h"

namespace {

class CurlTransaction : public sapi::Transaction {
 public:
  explicit CurlTransaction(std::unique_ptr<sapi::Sandbox> sandbox)
      : sapi::Transaction(std::move(sandbox)) {
    sapi::Transaction::SetTimeLimit(kTimeOutVal);
  }

 private:
  // Default timeout value for each transaction run.
  static constexpr time_t kTimeOutVal = 2;

  // The main processing function.
  absl::Status Main() override;
};

absl::Status CurlTransaction::Main() {
  curl::CurlApi api(sandbox());

  // Initialize the curl session
  SAPI_ASSIGN_OR_RETURN(void* curl_remote, api.curl_easy_init());
  sapi::v::RemotePtr curl(curl_remote);
  TRANSACTION_FAIL_IF_NOT(curl.GetValue(), "curl_easy_init failed");

  // Specify URL to get
  sapi::v::ConstCStr url("http://example.com");
  SAPI_ASSIGN_OR_RETURN(
      int setopt_url_code,
      api.curl_easy_setopt_ptr(&curl, curl::CURLOPT_URL, url.PtrBefore()));
  TRANSACTION_FAIL_IF_NOT(setopt_url_code == curl::CURLE_OK,
                          "curl_easy_setopt_ptr failed");

  // Perform the request
  SAPI_ASSIGN_OR_RETURN(int perform_code, api.curl_easy_perform(&curl));
  TRANSACTION_FAIL_IF_NOT(setopt_url_code == curl::CURLE_OK,
                          "curl_easy_perform failed");

  // Cleanup curl
  TRANSACTION_FAIL_IF_NOT(api.curl_easy_cleanup(&curl).ok(),
                          "curl_easy_cleanup failed");

  return absl::OkStatus();
}

}  // namespace

int main(int argc, char* argv[]) {
  CurlTransaction curl{std::make_unique<curl::CurlSapiSandbox>()};
  absl::Status status = curl.Run();
  CHECK(status.ok()) << "CurlTransaction failed";

  return EXIT_SUCCESS;
}
