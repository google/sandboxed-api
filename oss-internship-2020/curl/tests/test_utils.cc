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

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <memory>
#include <thread>

void CurlTestUtils::curl_test_set_up() {
  // Initialize sandbox2 and sapi
  sandbox = std::make_unique<CurlSapiSandbox>();
  sandbox->Init().IgnoreError();
  api = std::make_unique<CurlApi>(sandbox.get());

  // Initialize curl
  SAPI_ASSERT_OK_AND_ASSIGN(void* curl_raw_ptr, api->curl_easy_init());
  curl = std::make_unique<sapi::v::RemotePtr>(curl_raw_ptr);

  // Specify request URL
  sapi::v::ConstCStr sapi_url(kUrl.c_str());
  SAPI_ASSERT_OK_AND_ASSIGN(
      int setopt_url_code,
      api->curl_easy_setopt_ptr(curl.get(), CURLOPT_URL, sapi_url.PtrBefore()));
  EXPECT_EQ(setopt_url_code, CURLE_OK);

  // Set port
  SAPI_ASSERT_OK_AND_ASSIGN(
      int setopt_port_code,
      api->curl_easy_setopt_long(curl.get(), CURLOPT_PORT, port));
  EXPECT_EQ(setopt_port_code, CURLE_OK);

  // Generate pointer to the write_to_memory callback
  sapi::RPCChannel rpcc(sandbox->comms());
  size_t (*_function_ptr)(char*, size_t, size_t, void*);
  EXPECT_THAT(rpcc.Symbol("write_to_memory", (void**)&_function_ptr),
              sapi::IsOk());
  sapi::v::RemotePtr remote_function_ptr((void*)_function_ptr);

  // Set write_to_memory as the write function
  SAPI_ASSERT_OK_AND_ASSIGN(
      int setopt_write_function,
      api->curl_easy_setopt_ptr(curl.get(), CURLOPT_WRITEFUNCTION,
                                &remote_function_ptr));
  EXPECT_EQ(setopt_write_function, CURLE_OK);

  // Pass memory chunk object to the callback
  SAPI_ASSERT_OK_AND_ASSIGN(
      int setopt_write_data,
      api->curl_easy_setopt_ptr(curl.get(), CURLOPT_WRITEDATA,
                                chunk.PtrBoth()));
  EXPECT_EQ(setopt_write_data, CURLE_OK);
}

void CurlTestUtils::curl_test_tear_down() {
  // Cleanup curl
  api->curl_easy_cleanup(curl.get()).IgnoreError();
}

void CurlTestUtils::perform_request(std::string& response) {
  // Perform the request
  SAPI_ASSERT_OK_AND_ASSIGN(int perform_code,
                            api->curl_easy_perform(curl.get()));
  EXPECT_EQ(perform_code, CURLE_OK);

  // Get pointer to the memory chunk
  sapi::v::GenericPtr remote_ptr;
  remote_ptr.SetRemote(&((MemoryStruct*)chunk.GetRemote())->memory);
  sandbox->TransferFromSandboxee(&remote_ptr).IgnoreError();
  void* chunk_ptr = (void*)remote_ptr.GetValue();

  // Get the string and store it in response
  SAPI_ASSERT_OK_AND_ASSIGN(
      response, sandbox->GetCString(sapi::v::RemotePtr(chunk_ptr)));
}

void CurlTestUtils::perform_request() {
  // If the response is not needed, pass a string that will be discarded
  std::string discarded_response;
  perform_request(discarded_response);
}

void CurlTestUtils::start_mock_server() {
  // Get the socket file descriptor
  int socket_fd = socket(AF_INET, SOCK_STREAM, 0);

  // Create the socket address object
  // The port is set to 0, meaning that it will be auto assigned
  // Only local connections can access this socket
  sockaddr_in socket_address{AF_INET, 0, htonl(INADDR_LOOPBACK)};
  socklen_t socket_address_size = sizeof(socket_address);
  if (socket_fd == -1) return;  // future.valid() is false

  // Bind the file descriptor to the socket address object
  if (bind(socket_fd, (sockaddr*)&socket_address, socket_address_size) == -1)
    return;  // future.valid() is false

  // Assign an available port to the socket address object
  if (getsockname(socket_fd, (sockaddr*)&socket_address,
                  &socket_address_size) == -1)
    return;  // future.valid() is false

  // Get the port number
  port = ntohs(socket_address.sin_port);

  // Set server_future operation to socket listening
  server_future = std::async(std::launch::async, [=] {
    // Listen on the socket (maximum 1 connection)
    if (listen(socket_fd, 1) == -1) return false;

    // File descriptor to the connection socket
    // This blocks the thread until a connection is established
    int accepted_socket_fd = accept(socket_fd, (sockaddr*)&socket_address,
                                    (socklen_t*)&socket_address_size);
    if (accepted_socket_fd == -1) return false;

    // Read the request from the socket
    constexpr int kMaxRequestSize = 4096;
    char request[kMaxRequestSize] = {};
    if (read(accepted_socket_fd, request, kMaxRequestSize) == -1) return false;

    // Stop any other reading operation on the socket
    if (shutdown(accepted_socket_fd, SHUT_RD) == -1) return false;

    // Generate response depending on the HTTP method used
    std::string http_response =
        "HTTP/1.1 200 OK\nContent-Type: text/plain\nContent-Length: ";
    if (strncmp(request, "GET", 3) == 0) {
      http_response +=
          std::to_string(kSimpleResponse.size()) + "\n\n" + kSimpleResponse;
    } else if (strncmp(request, "POST", 4) == 0) {
      char* post_fields = strstr(request, "\r\n\r\n");
      post_fields += 4;  // Points to the first char after the HTTP header
      http_response += std::to_string(strlen(post_fields)) + "\n\n" +
                       std::string(post_fields);
    } else {
      return false;
    }

    // Write the response on the socket
    if (write(accepted_socket_fd, http_response.c_str(),
              http_response.size()) == -1)
      return false;

    // Close the socket
    if (close(accepted_socket_fd) == -1) return false;

    // No error was encountered, can return true
    return true;
  });
}
