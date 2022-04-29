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

// Sandboxed version of multithread.c
// Multithreaded HTTP GET requests

#include <cstdlib>
#include <future>  // NOLINT(build/c++11)
#include <thread>  // NOLINT(build/c++11)

#include "../curl_util.h"    // NOLINT(build/include)
#include "../sandbox.h"      // NOLINT(build/include)
#include "curl_sapi.sapi.h"  // NOLINT(build/include)
#include "absl/strings/str_cat.h"
#include "sandboxed_api/util/status_macros.h"

namespace {

absl::Status pull_one_url(const std::string& url, curl::CurlApi& api) {
  // Initialize the curl session
  curl::CURL* curl_handle;
  SAPI_ASSIGN_OR_RETURN(curl_handle, api.curl_easy_init());
  sapi::v::RemotePtr curl(curl_handle);
  if (!curl_handle) {
    return absl::UnavailableError("curl_easy_init failed: Invalid curl handle");
  }

  int curl_code;

  // Specify URL to get
  sapi::v::ConstCStr sapi_url(url.c_str());
  SAPI_ASSIGN_OR_RETURN(
      curl_code,
      api.curl_easy_setopt_ptr(&curl, curl::CURLOPT_URL, sapi_url.PtrBefore()));
  if (curl_code != 0) {
    return absl::UnavailableError(absl::StrCat(
        "curl_easy_setopt_ptr failed: ", curl::StrError(&api, curl_code)));
  }

  // Perform the request
  SAPI_ASSIGN_OR_RETURN(curl_code, api.curl_easy_perform(&curl));
  if (curl_code != 0) {
    return absl::UnavailableError(absl::StrCat(
        "curl_easy_perform failed: ", curl::StrError(&api, curl_code)));
  }

  // Cleanup curl easy handle
  SAPI_RETURN_IF_ERROR(api.curl_easy_cleanup(&curl));

  return absl::OkStatus();
}

absl::Status Example5() {
  // Initialize sandbox2 and sapi
  curl::CurlSapiSandbox sandbox;
  SAPI_RETURN_IF_ERROR(sandbox.Init());
  curl::CurlApi api(&sandbox);

  int curl_code;

  // Initialize curl (CURL_GLOBAL_DEFAULT = 3)
  SAPI_ASSIGN_OR_RETURN(curl_code, api.curl_global_init(3l));
  if (curl_code != 0) {
    return absl::UnavailableError(absl::StrCat(
        "curl_global_init failed: ", curl::StrError(&api, curl_code)));
  }

  // Create the threads (by using futures)
  const std::vector<std::string> urls = {
      "http://example.com", "http://example.edu", "http://example.net",
      "http://example.org"};
  std::vector<std::future<absl::Status>> futures;
  for (auto& url : urls) {
    futures.emplace_back(
        std::async(pull_one_url, std::ref(url), std::ref(api)));
  }

  // Join the threads and check for errors
  for (auto& future : futures) {
    SAPI_RETURN_IF_ERROR(future.get());
  }

  // Cleanup curl
  SAPI_RETURN_IF_ERROR(api.curl_global_cleanup());

  return absl::OkStatus();
}

}  // namespace

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  sapi::InitLogging(argv[0]);

  if (absl::Status status = Example5(); !status.ok()) {
    LOG(ERROR) << "Example5 failed: " << status.ToString();
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
