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

#ifndef TESTS_H
#define TESTS_H

#include "../sandbox.h"
#include "curl_sapi.sapi.h"
#include "gtest/gtest.h"
#include "sandboxed_api/util/flag.h"
#include "sandboxed_api/util/status_matchers.h"

// Helper class that can be used to test Curl Sandboxed
class CurlTestUtils {
 protected:
  struct MemoryStruct {
    char* memory;
    size_t size;
  };

  // Initialize and set up the curl handle
  absl::Status CurlTestSetUp();
  // Clean up the curl handle
  absl::Status CurlTestTearDown();

  // Perform a request to the mock server, return the response
  absl::StatusOr<std::string> PerformRequest();

  // Start a mock server (only once) that will manage connections for the tests
  // The server listens on a port asynchronously by creating a thread
  // The port number is stored in port_
  // Responds with kSimpleResponse to a GET request
  // Responds with the POST request fields to a POST request
  static void StartMockServer();

  std::unique_ptr<CurlSapiSandbox> sandbox_;
  std::unique_ptr<CurlApi> api_;
  std::unique_ptr<sapi::v::RemotePtr> curl_;

  static std::thread server_thread_;

  static constexpr absl::string_view kUrl = "http://127.0.0.1/";
  static long port_;

  static constexpr absl::string_view kSimpleResponse = "OK";

 private:
  sapi::v::LenVal chunk_{0};
};

#endif  // TESTS_H
