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

#include <future>

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
  void curl_test_set_up();
  // Clean up the curl handle
  void curl_test_tear_down();

  // Perform a request to the mock server
  // Optionally, store the response in a string
  void perform_request(std::string& response);
  void perform_request();

  // Start a mock server that will manage connections for the tests
  // The server listens on a port asynchronously by creating a future
  // server_future.valid() is false if an error occured in the original thread
  // server_future.get() is false if an error occured in the server thread
  // The port number is stored in port
  // Responds with kSimpleResponse to a GET request
  // Responds with the POST request fields to a POST request
  void start_mock_server();

  std::unique_ptr<CurlSapiSandbox> sandbox;
  std::unique_ptr<CurlApi> api;
  std::unique_ptr<sapi::v::RemotePtr> curl;

  std::future<bool> server_future;

  const std::string kUrl = "http://127.0.0.1/";
  long port;

  const std::string kSimpleResponse = "OK";

  sapi::v::Struct<MemoryStruct> chunk;
};

#endif  // TESTS_H
