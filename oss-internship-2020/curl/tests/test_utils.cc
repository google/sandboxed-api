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

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <thread>

long int CurlTestUtils::port_;
std::thread CurlTestUtils::server_thread_;

void CurlTestUtils::CurlTestSetUp() {
  // Initialize sandbox2 and sapi
  sandbox_ = std::make_unique<CurlSapiSandbox>();
  ASSERT_THAT(sandbox_->Init(), sapi::IsOk());
  api_ = std::make_unique<CurlApi>(sandbox_.get());

  // Initialize curl
  SAPI_ASSERT_OK_AND_ASSIGN(void* curl_raw_ptr, api_->curl_easy_init());
  curl_ = std::make_unique<sapi::v::RemotePtr>(curl_raw_ptr);

  // Specify request URL
  sapi::v::ConstCStr sapi_url(kUrl.data());
  SAPI_ASSERT_OK_AND_ASSIGN(int setopt_url_code,
                            api_->curl_easy_setopt_ptr(curl_.get(), CURLOPT_URL,
                                                       sapi_url.PtrBefore()));
  ASSERT_EQ(setopt_url_code, CURLE_OK);

  // Set port
  SAPI_ASSERT_OK_AND_ASSIGN(
      int setopt_port_code,
      api_->curl_easy_setopt_long(curl_.get(), CURLOPT_PORT, port_));
  ASSERT_EQ(setopt_port_code, CURLE_OK);

  // Generate pointer to the WriteToMemory callback
  sapi::RPCChannel rpcc(sandbox_->comms());
  size_t (*_function_ptr)(char*, size_t, size_t, void*);
  ASSERT_THAT(rpcc.Symbol("WriteToMemory", (void**)&_function_ptr),
              sapi::IsOk());
  sapi::v::RemotePtr remote_function_ptr((void*)_function_ptr);

  // Set WriteToMemory as the write function
  SAPI_ASSERT_OK_AND_ASSIGN(
      int setopt_write_function,
      api_->curl_easy_setopt_ptr(curl_.get(), CURLOPT_WRITEFUNCTION,
                                 &remote_function_ptr));
  ASSERT_EQ(setopt_write_function, CURLE_OK);

  // Pass memory chunk object to the callback
  SAPI_ASSERT_OK_AND_ASSIGN(
      int setopt_write_data,
      api_->curl_easy_setopt_ptr(curl_.get(), CURLOPT_WRITEDATA,
                                 chunk_.PtrBoth()));
  ASSERT_EQ(setopt_write_data, CURLE_OK);
}

void CurlTestUtils::CurlTestTearDown() {
  // Cleanup curl
  ASSERT_THAT(api_->curl_easy_cleanup(curl_.get()), sapi::IsOk());
}

void CurlTestUtils::PerformRequest(std::string& response) {
  // Perform the request
  SAPI_ASSERT_OK_AND_ASSIGN(int perform_code,
                            api_->curl_easy_perform(curl_.get()));
  ASSERT_EQ(perform_code, CURLE_OK);

  // Get pointer to the memory chunk
  sapi::v::GenericPtr remote_ptr;
  remote_ptr.SetRemote(&((MemoryStruct*)chunk_.GetRemote())->memory);
  ASSERT_THAT(sandbox_->TransferFromSandboxee(&remote_ptr), sapi::IsOk());
  void* chunk_ptr = (void*)remote_ptr.GetValue();

  // Get the string and store it in response
  SAPI_ASSERT_OK_AND_ASSIGN(
      response, sandbox_->GetCString(sapi::v::RemotePtr(chunk_ptr)));
}

void CurlTestUtils::PerformRequest() {
  // If the response is not needed, pass a string that will be discarded
  std::string discarded_response;
  PerformRequest(discarded_response);
}

void CurlTestUtils::StartMockServer() {
  // Get ai_list, a list of addrinfo structures, the port will be set later
  addrinfo hints{AI_PASSIVE, AF_INET, SOCK_STREAM};
  addrinfo* ai_list;
  if (getaddrinfo("127.0.0.1", NULL, &hints, &ai_list) < 0) {
    return;
  }

  // Loop over ai_list, until a socket is created
  int listening_socket;
  for (addrinfo* p = ai_list;; p = p->ai_next) {
    if (p == nullptr) {
      return;
    }

    // Try creating a socket
    listening_socket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (listening_socket >= 0) {
      // Try binding the socket to the address
      if (bind(listening_socket, p->ai_addr, p->ai_addrlen) >= 0) {
        // Assign an arbitrary available port to the socket address object
        if (getsockname(listening_socket, p->ai_addr, &p->ai_addrlen) == -1) {
          return;
        }
        port_ = ntohs(((struct sockaddr_in*)p->ai_addr)->sin_port);

        break;

      } else {
        close(listening_socket);
      }
    }
  }

  freeaddrinfo(ai_list);

  // Listen on the socket
  if (listen(listening_socket, 256) == -1) {
    return;
  }

  // Set server_thread_ operation to socket listening
  server_thread_ = std::thread([=] {
    // Create the master fd_set containing listening_socket
    fd_set master_fd_set;
    FD_ZERO(&master_fd_set);
    FD_SET(listening_socket, &master_fd_set);

    // Create an empty fd_set, will be used for making copies of master_fd_set
    fd_set copy_fd_set;
    FD_ZERO(&copy_fd_set);

    int max_fd = listening_socket;

    // Keep calling select and block after a new event happens
    // Doesn't stop until the process doing tests is terminated
    for (;;) {
      copy_fd_set = master_fd_set;

      // Block and wait for a file descriptor to be ready
      if (select(max_fd + 1, &copy_fd_set, nullptr, nullptr, nullptr) == -1) {
        close(listening_socket);
        return;
      }

      // A file descriptor is ready, loop over all the fds to find it
      for (int i = 0; i <= max_fd; ++i) {
        // i is not a file desciptor in the set, skip it
        if (!FD_ISSET(i, &copy_fd_set)) {
          continue;
        }

        if (i == listening_socket) {  // CASE 1: a new connection

          sockaddr_storage remote_address;
          socklen_t remote_address_size = sizeof(remote_address);

          // Accept the connection
          int accepted_socket =
              accept(listening_socket, (sockaddr*)&remote_address,
                     &remote_address_size);
          if (accepted_socket == -1) {
            close(listening_socket);
            return;
          }

          // Add the new socket to the fd_set and update max_fd
          FD_SET(accepted_socket, &master_fd_set);
          max_fd = std::max(max_fd, accepted_socket);

        } else {  // CASE 2: a request from an existing connection

          constexpr int kMaxRequestSize = 4096;
          char buf[kMaxRequestSize] = {};

          // Receive message from socket
          int num_bytes = recv(i, buf, sizeof(buf), 0);
          if (num_bytes == -1) {
            close(listening_socket);
            return;

          } else if (num_bytes == 0) {
            // Close the connection and remove it from fd_set
            close(i);
            FD_CLR(i, &master_fd_set);

          } else {
            // Prepare a response for the request
            std::string http_response =
                "HTTP/1.1 200 OK\nContent-Type: text/plain\nContent-Length: ";

            if (strncmp(buf, "GET", 3) == 0) {
              http_response += std::to_string(kSimpleResponse.size()) + "\n\n" +
                               std::string{kSimpleResponse};

            } else if (strncmp(buf, "POST", 4) == 0) {
              char* post_fields = strstr(buf, "\r\n\r\n");
              post_fields += 4;  // Points to the first char after HTTP header
              http_response += std::to_string(strlen(post_fields)) + "\n\n" +
                               std::string(post_fields);

            } else {
              close(listening_socket);
              return;
            }

            // Write the response to the request
            if (write(i, http_response.c_str(), http_response.size()) == -1) {
              close(listening_socket);
              return;
            }
          }
        }
      }
    }
  });
}
