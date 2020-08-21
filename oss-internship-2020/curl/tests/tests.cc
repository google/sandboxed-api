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

#include "test_utils.h"

class Curl_Test : public CurlTestUtils, public ::testing::Test {
 protected:
  void SetUp() override {
    // Start mock server, also sets port number
    start_mock_server();
    ASSERT_TRUE(server_future.valid());

    curl_test_set_up();
  }

  void TearDown() override {
    curl_test_tear_down();

    // Wait for the mock server to return and check for any error
    ASSERT_TRUE(server_future.get());
  }
};

TEST_F(Curl_Test, EffectiveUrl) {
  sapi::v::RemotePtr effective_url_ptr(nullptr);

  perform_request();

  // Get effective URL
  SAPI_ASSERT_OK_AND_ASSIGN(
      int getinfo_code,
      api->curl_easy_getinfo_ptr(curl.get(), CURLINFO_EFFECTIVE_URL,
                                 effective_url_ptr.PtrBoth()));
  ASSERT_EQ(getinfo_code, CURLE_OK);

  // Store effective URL in a string
  SAPI_ASSERT_OK_AND_ASSIGN(std::string effective_url,
                            sandbox->GetCString(sapi::v::RemotePtr(
                                effective_url_ptr.GetPointedVar())));

  // Compare effective URL with original URL
  ASSERT_EQ(effective_url, kUrl);
}

TEST_F(Curl_Test, EffectivePort) {
  sapi::v::Int effective_port;

  perform_request();

  // Get effective port
  SAPI_ASSERT_OK_AND_ASSIGN(
      int getinfo_code,
      api->curl_easy_getinfo_ptr(curl.get(), CURLINFO_PRIMARY_PORT,
                                 effective_port.PtrBoth()));
  ASSERT_EQ(getinfo_code, CURLE_OK);

  // Compare effective port with port set by the mock server
  ASSERT_EQ(effective_port.GetValue(), port);
}

TEST_F(Curl_Test, ResponseCode) {
  sapi::v::Int response_code;

  perform_request();

  // Get response code
  SAPI_ASSERT_OK_AND_ASSIGN(
      int getinfo_code,
      api->curl_easy_getinfo_ptr(curl.get(), CURLINFO_RESPONSE_CODE,
                                 response_code.PtrBoth()));
  ASSERT_EQ(getinfo_code, CURLE_OK);

  // Check response code
  ASSERT_EQ(response_code.GetValue(), 200);
}

TEST_F(Curl_Test, ContentType) {
  sapi::v::RemotePtr content_type_ptr(nullptr);

  perform_request();

  // Get effective URL
  SAPI_ASSERT_OK_AND_ASSIGN(
      int getinfo_code,
      api->curl_easy_getinfo_ptr(curl.get(), CURLINFO_CONTENT_TYPE,
                                 content_type_ptr.PtrBoth()));
  ASSERT_EQ(getinfo_code, CURLE_OK);

  // Store content type in a string
  SAPI_ASSERT_OK_AND_ASSIGN(std::string content_type,
                            sandbox->GetCString(sapi::v::RemotePtr(
                                content_type_ptr.GetPointedVar())));

  // Compare content type with "text/plain"
  ASSERT_EQ(content_type, "text/plain");
}

TEST_F(Curl_Test, GETResponse) {
  std::string response;
  perform_request(response);

  // Compare response with expected response
  ASSERT_EQ(response, kSimpleResponse);
}

TEST_F(Curl_Test, POSTResponse) {
  sapi::v::ConstCStr post_fields("postfields");

  // Set request method to POST
  SAPI_ASSERT_OK_AND_ASSIGN(int setopt_post, api->curl_easy_setopt_long(
                                                 curl.get(), CURLOPT_POST, 1l));
  ASSERT_EQ(setopt_post, CURLE_OK);

  // Set the size of the POST fields
  SAPI_ASSERT_OK_AND_ASSIGN(
      int setopt_post_fields_size,
      api->curl_easy_setopt_long(curl.get(), CURLOPT_POSTFIELDSIZE,
                                 post_fields.GetSize()));
  ASSERT_EQ(setopt_post_fields_size, CURLE_OK);

  // Set the POST fields
  SAPI_ASSERT_OK_AND_ASSIGN(
      int setopt_post_fields,
      api->curl_easy_setopt_ptr(curl.get(), CURLOPT_POSTFIELDS,
                                post_fields.PtrBefore()));
  ASSERT_EQ(setopt_post_fields, CURLE_OK);

  std::string response;
  perform_request(response);

  // Compare response with expected response
  ASSERT_EQ(std::string(post_fields.GetData()), response);
}
