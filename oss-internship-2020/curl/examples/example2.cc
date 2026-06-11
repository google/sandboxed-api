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

// Sandboxed version of getinmemory.c
// HTTP GET request using callbacks

#include <cstdlib>

#include "../curl_util.h"  // NOLINT(build/include)
#include "../sandbox.h"    // NOLINT(build/include)
#include "absl/strings/str_cat.h"
#include "sandboxed_api/util/status_macros.h"

namespace {

absl::Status Example2() {
  // Initialize sandbox2 and sapi
  curl::CurlSapiSandbox sandbox;
  SAPI_RETURN_IF_ERROR(sandbox.Init());
  curl::CurlApi api(&sandbox);

  // Generate pointer to WriteMemoryCallback function
  void* function_ptr;
  SAPI_RETURN_IF_ERROR(
      sandbox.rpc_channel()->Symbol("WriteToMemory", &function_ptr));
  sapi::v::RemotePtr write_to_memory(function_ptr);

  // Initialize the curl session
  curl::CURL* curl_handle;
  SAPI_ASSIGN_OR_RETURN(curl_handle, api.curl_easy_init());
  sapi::v::RemotePtr curl(curl_handle);
  if (!curl_handle) {
    return absl::UnavailableError("curl_easy_init failed: Invalid curl handle");
  }

  int curl_code;

  // Specify URL to get
  sapi::v::ConstCStr url("http://example.com");
  SAPI_ASSIGN_OR_RETURN(
      curl_code,
      api.curl_easy_setopt_ptr(&curl, curl::CURLOPT_URL, url.PtrBefore()));
  if (curl_code != 0) {
    return absl::UnavailableError(absl::StrCat(
        "curl_easy_setopt_ptr failed: ", curl::StrError(&api, curl_code)));
  }

  // Set WriteMemoryCallback as the write function
  SAPI_ASSIGN_OR_RETURN(
      curl_code, api.curl_easy_setopt_ptr(&curl, curl::CURLOPT_WRITEFUNCTION,
                                          &write_to_memory));
  if (curl_code != 0) {
    return absl::UnavailableError(absl::StrCat(
        "curl_easy_setopt_ptr failed: ", curl::StrError(&api, curl_code)));
  }

  // Pass 'chunk' struct to the callback function
  sapi::v::LenVal chunk(0);
  SAPI_ASSIGN_OR_RETURN(curl_code,
                        api.curl_easy_setopt_ptr(&curl, curl::CURLOPT_WRITEDATA,
                                                 chunk.PtrBoth()));
  if (curl_code != 0) {
    return absl::UnavailableError(absl::StrCat(
        "curl_easy_setopt_ptr failed: ", curl::StrError(&api, curl_code)));
  }

  // Set a user agent
  sapi::v::ConstCStr user_agent("libcurl-agent/1.0");
  SAPI_ASSIGN_OR_RETURN(curl_code,
                        api.curl_easy_setopt_ptr(&curl, curl::CURLOPT_USERAGENT,
                                                 user_agent.PtrBefore()));
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

  // Retrieve memory size
  SAPI_RETURN_IF_ERROR(sandbox.TransferFromSandboxee(&chunk));
  std::cout << "memory size: " << chunk.GetDataSize() << " bytes" << std::endl;

  // Cleanup curl
  SAPI_RETURN_IF_ERROR(api.curl_easy_cleanup(&curl));

  return absl::OkStatus();
}

}  // namespace

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  sapi::InitLogging(argv[0]);

  if (absl::Status status = Example2(); !status.ok()) {
    LOG(ERROR) << "Example2 failed: " << status.ToString();
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
