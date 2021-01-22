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

#include "test_utils.h"  // NOLINT(build/include)

namespace {

class CurlTest : public curl::tests::CurlTestUtils, public ::testing::Test {
 protected:
  void SetUp() override {
    // Start mock server, get port number and check for any error
    StartMockServer();
    ASSERT_TRUE(server_thread_.joinable());
    ASSERT_TRUE(CurlTestSetUp().ok());
  }

  void TearDown() override {
    ASSERT_TRUE(CurlTestTearDown().ok());
    // Detach the server thread
    server_thread_.detach();
  }
};

TEST_F(CurlTest, EffectiveUrl) {
  sapi::v::RemotePtr effective_url_ptr(nullptr);

  ASSERT_TRUE(PerformRequest().ok());

  // Get effective URL
  SAPI_ASSERT_OK_AND_ASSIGN(
      int getinfo_code,
      api_->curl_easy_getinfo_ptr(curl_.get(), curl::CURLINFO_EFFECTIVE_URL,
                                  effective_url_ptr.PtrBoth()));
  ASSERT_EQ(getinfo_code, curl::CURLE_OK);

  // Store effective URL in a string
  SAPI_ASSERT_OK_AND_ASSIGN(std::string effective_url,
                            sandbox_->GetCString(sapi::v::RemotePtr(
                                effective_url_ptr.GetPointedVar())));

  // Compare effective URL with original URL
  ASSERT_EQ(effective_url, kUrl);
}

TEST_F(CurlTest, EffectivePort) {
  sapi::v::Int effective_port;

  ASSERT_TRUE(PerformRequest().ok());

  // Get effective port
  SAPI_ASSERT_OK_AND_ASSIGN(
      int getinfo_code,
      api_->curl_easy_getinfo_ptr(curl_.get(), curl::CURLINFO_PRIMARY_PORT,
                                  effective_port.PtrBoth()));
  ASSERT_EQ(getinfo_code, curl::CURLE_OK);

  // Compare effective port with port set by the mock server
  ASSERT_EQ(effective_port.GetValue(), port_);
}

TEST_F(CurlTest, ResponseCode) {
  sapi::v::Int response_code;

  ASSERT_TRUE(PerformRequest().ok());

  // Get response code
  SAPI_ASSERT_OK_AND_ASSIGN(
      int getinfo_code,
      api_->curl_easy_getinfo_ptr(curl_.get(), curl::CURLINFO_RESPONSE_CODE,
                                  response_code.PtrBoth()));
  ASSERT_EQ(getinfo_code, curl::CURLE_OK);

  // Check response code
  ASSERT_EQ(response_code.GetValue(), 200);
}

TEST_F(CurlTest, ContentType) {
  sapi::v::RemotePtr content_type_ptr(nullptr);

  ASSERT_TRUE(PerformRequest().ok());

  // Get effective URL
  SAPI_ASSERT_OK_AND_ASSIGN(
      int getinfo_code,
      api_->curl_easy_getinfo_ptr(curl_.get(), curl::CURLINFO_CONTENT_TYPE,
                                  content_type_ptr.PtrBoth()));
  ASSERT_EQ(getinfo_code, curl::CURLE_OK);

  // Store content type in a string
  SAPI_ASSERT_OK_AND_ASSIGN(std::string content_type,
                            sandbox_->GetCString(sapi::v::RemotePtr(
                                content_type_ptr.GetPointedVar())));

  // Compare content type with "text/plain"
  ASSERT_EQ(content_type, "text/plain");
}

TEST_F(CurlTest, GETResponse) {
  SAPI_ASSERT_OK_AND_ASSIGN(std::string response, PerformRequest());

  // Compare response with expected response
  ASSERT_EQ(response, "OK");
}

TEST_F(CurlTest, POSTResponse) {
  sapi::v::ConstCStr post_fields("postfields");

  // Set request method to POST
  SAPI_ASSERT_OK_AND_ASSIGN(
      int setopt_post,
      api_->curl_easy_setopt_long(curl_.get(), curl::CURLOPT_POST, 1l));
  ASSERT_EQ(setopt_post, curl::CURLE_OK);

  // Set the size of the POST fields
  SAPI_ASSERT_OK_AND_ASSIGN(
      int setopt_post_fields_size,
      api_->curl_easy_setopt_long(curl_.get(), curl::CURLOPT_POSTFIELDSIZE,
                                  post_fields.GetSize()));
  ASSERT_EQ(setopt_post_fields_size, curl::CURLE_OK);

  // Set the POST fields
  SAPI_ASSERT_OK_AND_ASSIGN(
      int setopt_post_fields,
      api_->curl_easy_setopt_ptr(curl_.get(), curl::CURLOPT_POSTFIELDS,
                                 post_fields.PtrBefore()));
  ASSERT_EQ(setopt_post_fields, curl::CURLE_OK);

  SAPI_ASSERT_OK_AND_ASSIGN(std::string response, PerformRequest());

  // Compare response with expected response
  ASSERT_EQ(std::string(post_fields.GetData()), response);
}

}  // namespace
