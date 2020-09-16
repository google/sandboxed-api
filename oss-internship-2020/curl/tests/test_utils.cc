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

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <thread>

long int CurlTestUtils::port_;
std::thread CurlTestUtils::server_thread_;

absl::Status CurlTestUtils::CurlTestSetUp() {
  // Initialize sandbox2 and sapi
  sandbox_ = std::make_unique<CurlSapiSandbox>();
  absl::Status init = sandbox_->Init();
  if (!init.ok()) {
    return init;
  }
  api_ = std::make_unique<CurlApi>(sandbox_.get());

  // Initialize curl
  absl::StatusOr<CURL*> curl_handle = api_->curl_easy_init();
  if (!curl_handle.ok()) {
    return curl_handle.status();
  } else if (!curl_handle.value()) {
    return absl::UnavailableError("curl_easy_init returned NULL ");
  }
  curl_ = std::make_unique<sapi::v::RemotePtr>(curl_handle.value());

  absl::StatusOr<int> curl_code;

  // Specify request URL
  sapi::v::ConstCStr sapi_url(kUrl.data());
  curl_code = api_->curl_easy_setopt_ptr(curl_.get(), CURLOPT_URL,
                                         sapi_url.PtrBefore());
  if (!curl_code.ok()) {
    return curl_code.status();
  } else if (curl_code.value() != CURLE_OK) {
    return absl::UnavailableError(
        "curl_easy_setopt_ptr returned with the error code " +
        curl_code.value());
  }

  // Set port
  curl_code = api_->curl_easy_setopt_long(curl_.get(), CURLOPT_PORT, port_);
  if (!curl_code.ok()) {
    return curl_code.status();
  } else if (curl_code.value() != CURLE_OK) {
    return absl::UnavailableError(
        "curl_easy_setopt_long returned with the error code " +
        curl_code.value());
  }

  // Generate pointer to the WriteToMemory callback
  void* function_ptr;
  absl::Status symbol =
      sandbox_->rpc_channel()->Symbol("WriteToMemoryTests", &function_ptr);
  if (!symbol.ok()) {
    return symbol;
  }
  sapi::v::RemotePtr remote_function_ptr(function_ptr);

  // Set WriteToMemory as the write function
  curl_code = api_->curl_easy_setopt_ptr(curl_.get(), CURLOPT_WRITEFUNCTION,
                                         &remote_function_ptr);
  if (!curl_code.ok()) {
    return curl_code.status();
  } else if (curl_code.value() != CURLE_OK) {
    return absl::UnavailableError(
        "curl_easy_setopt_ptr returned with the error code " +
        curl_code.value());
  }

  // Pass memory chunk object to the callback
  curl_code = api_->curl_easy_setopt_ptr(curl_.get(), CURLOPT_WRITEDATA,
                                         chunk_.PtrBoth());
  if (!curl_code.ok()) {
    return curl_code.status();
  } else if (curl_code.value() != CURLE_OK) {
    return absl::UnavailableError(
        "curl_easy_setopt_ptr returned with the error code " +
        curl_code.value());
  }

  return absl::OkStatus();
}

absl::Status CurlTestUtils::CurlTestTearDown() {
  // Cleanup curl
  return api_->curl_easy_cleanup(curl_.get());
}

absl::StatusOr<std::string> CurlTestUtils::PerformRequest() {
  // Perform the request
  absl::StatusOr<int> curl_code = api_->curl_easy_perform(curl_.get());
  if (!curl_code.ok()) {
    return curl_code.status();
  } else if (curl_code.value() != CURLE_OK) {
    return absl::UnavailableError(
        "curl_easy_perform returned with the error code " + curl_code.value());
  }

  // Get pointer to the memory chunk
  sapi::v::GenericPtr remote_ptr;
  remote_ptr.SetRemote(
      &(static_cast<sapi::LenValStruct*>(chunk_.GetRemote()))->data);
  absl::Status transfer = sandbox_->TransferFromSandboxee(&remote_ptr);
  if (!transfer.ok()) {
    return transfer;
  }
  void* chunk_ptr = (void*)remote_ptr.GetValue();

  // Get the string
  absl::StatusOr<std::string> response =
      sandbox_->GetCString(sapi::v::RemotePtr(chunk_ptr));
  if (!response.ok()) {
    return response.status();
  }

  return response.value();
}

// Read the socket until str is completely read
std::string ReadUntil(const int socket, const std::string& str,
                      const size_t max_request_size) {
  char buf[max_request_size] = {};
  size_t read_bytes = 0;

  // Read one char at a time until str is suffix of buf
  do {
    if (read_bytes >= max_request_size ||
        read(socket, buf + read_bytes, 1) == -1) {
      return "";
    }
    ++read_bytes;
  } while (std::string{buf + std::max(size_t{0}, read_bytes - str.size())} !=
           str);

  buf[read_bytes] = '\0';
  return std::string{buf};
}

// Parse HTTP headers to return the Content-Length
size_t GetContentLength(const std::string& headers) {
  // Find the Content-Length header
  const char* length_header_start = strstr(headers.c_str(), "Content-Length: ");

  // There is no Content-Length field
  if (!length_header_start) {
    return 0;
  }

  // Find Content-Length string
  const char* length_start = length_header_start + strlen("Content-Length: ");
  size_t length_bytes = strstr(length_start, "\r\n") - length_start;
  if (length_bytes >= 64) {
    return 0;
  }

  // Convert Content-Length string value to int
  char content_length_string[64];
  strncpy(content_length_string, length_start, length_bytes);

  return atoi(content_length_string);
}

// Read exactly content_bytes from the socket
std::string ReadExact(int socket, size_t content_bytes) {
  char buf[content_bytes + 1] = {};
  size_t read_bytes = 0;

  // Read until content_bytes chars are read
  do {
    int num_bytes;
    if ((num_bytes = read(socket, buf + read_bytes,
                          sizeof(buf) - read_bytes - 1)) == -1) {
      return "";
    }
    read_bytes += num_bytes;
  } while (read_bytes < content_bytes);

  buf[content_bytes] = '\0';

  return std::string{buf};
}

void CurlTestUtils::StartMockServer() {
  // Get the socket file descriptor
  int listening_socket = socket(AF_INET, SOCK_STREAM, 0);

  // Create the socket address object
  // The port is set to 0, meaning that it will be auto assigned
  // Only local connections can access this socket
  sockaddr_in socket_address{AF_INET, 0, htonl(INADDR_LOOPBACK)};
  socklen_t socket_address_size = sizeof(socket_address);
  if (listening_socket == -1) {
    return;
  }

  // Bind the file descriptor to the socket address object
  if (bind(listening_socket, (sockaddr*)&socket_address, socket_address_size) ==
      -1) {
    return;
  }

  // Assign an available port to the socket address object
  if (getsockname(listening_socket, (sockaddr*)&socket_address,
                  &socket_address_size) == -1) {
    return;
  }

  // Get the port number
  port_ = ntohs(socket_address.sin_port);

  // Set server_thread_ operation to socket listening
  server_thread_ = std::thread([=] {
    // Listen on the socket (maximum 1 connection)
    if (listen(listening_socket, 1) == -1) {
      return;
    }

    // File descriptor to the connection socket
    // This blocks the thread until a connection is established
    int accepted_socket = accept(listening_socket, (sockaddr*)&socket_address,
                                 (socklen_t*)&socket_address_size);
    if (accepted_socket == -1) {
      return;
    }

    constexpr int kMaxRequestSize = 4096;

    // Read until the end of the headers
    std::string headers =
        ReadUntil(accepted_socket, "\r\n\r\n", kMaxRequestSize);

    // Get the length of the request content
    size_t content_length = GetContentLength(headers);
    if (content_length > kMaxRequestSize - headers.size()) {
      close(accepted_socket);
      return;
    }

    // Read the request content
    std::string content = ReadExact(accepted_socket, content_length);

    // Prepare a response for the request
    std::string http_response =
        "HTTP/1.1 200 OK\nContent-Type: text/plain\nContent-Length: ";

    if (headers.substr(0, 3) == "GET") {
      http_response += std::to_string(kSimpleResponse.size()) + "\r\n\r\n" +
                       std::string{kSimpleResponse};

    } else if (headers.substr(0, 4) == "POST") {
      http_response +=
          std::to_string(content.size()) + "\r\n\r\n" + std::string{content};

    } else {
      close(accepted_socket);
      return;
    }

    // Ignore any errors, the connection will be closed anyway
    write(accepted_socket, http_response.c_str(), http_response.size());

    // Close the socket
    close(accepted_socket);
  });
}
