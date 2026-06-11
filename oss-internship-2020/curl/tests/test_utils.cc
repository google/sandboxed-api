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

#include "test_utils.h"  // NOLINT(build/include)

#include <absl/strings/match.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <thread>  // NOLINT(build/c++11)

#include "absl/status/statusor.h"
#include "sandboxed_api/util/status_macros.h"

namespace curl::tests {

int CurlTestUtils::port_;

std::thread CurlTestUtils::server_thread_;

absl::Status CurlTestUtils::CurlTestSetUp() {
  // Initialize sandbox2 and SAPI
  sandbox_ = std::make_unique<curl::CurlSapiSandbox>();
  SAPI_RETURN_IF_ERROR(sandbox_->Init());
  api_ = std::make_unique<curl::CurlApi>(sandbox_.get());

  // Initialize curl
  SAPI_ASSIGN_OR_RETURN(curl::CURL * curl_handle, api_->curl_easy_init());
  if (!curl_handle) {
    return absl::UnavailableError("curl_easy_init returned nullptr");
  }
  curl_ = std::make_unique<sapi::v::RemotePtr>(curl_handle);

  int curl_code = 0;

  // Specify request URL
  sapi::v::ConstCStr sapi_url(kUrl);
  SAPI_ASSIGN_OR_RETURN(
      curl_code, api_->curl_easy_setopt_ptr(curl_.get(), curl::CURLOPT_URL,
                                            sapi_url.PtrBefore()));
  if (curl_code != curl::CURLE_OK) {
    return absl::UnavailableError(absl::StrCat(
        "curl_easy_setopt_ptr returned with the error code ", curl_code));
  }

  // Set port
  SAPI_ASSIGN_OR_RETURN(curl_code, api_->curl_easy_setopt_long(
                                       curl_.get(), curl::CURLOPT_PORT, port_));
  if (curl_code != curl::CURLE_OK) {
    return absl::UnavailableError(absl::StrCat(
        "curl_easy_setopt_long returned with the error code ", curl_code));
  }

  // Generate pointer to the WriteToMemory callback
  void* function_ptr;
  SAPI_RETURN_IF_ERROR(
      sandbox_->rpc_channel()->Symbol("WriteToMemory", &function_ptr));
  sapi::v::RemotePtr remote_function_ptr(function_ptr);

  // Set WriteToMemory as the write function
  SAPI_ASSIGN_OR_RETURN(curl_code, api_->curl_easy_setopt_ptr(
                                       curl_.get(), curl::CURLOPT_WRITEFUNCTION,
                                       &remote_function_ptr));
  if (curl_code != curl::CURLE_OK) {
    return absl::UnavailableError(absl::StrCat(
        "curl_easy_setopt_ptr returned with the error code ", curl_code));
  }

  // Pass memory chunk object to the callback
  chunk_ = std::make_unique<sapi::v::LenVal>(0);
  SAPI_ASSIGN_OR_RETURN(
      curl_code, api_->curl_easy_setopt_ptr(
                     curl_.get(), curl::CURLOPT_WRITEDATA, chunk_->PtrBoth()));
  if (curl_code != curl::CURLE_OK) {
    return absl::UnavailableError(absl::StrCat(
        "curl_easy_setopt_ptr returned with the error code ", curl_code));
  }

  return absl::OkStatus();
}

absl::Status CurlTestUtils::CurlTestTearDown() {
  // Cleanup curl
  return api_->curl_easy_cleanup(curl_.get());
}

absl::StatusOr<std::string> CurlTestUtils::PerformRequest() {
  // Perform the request
  SAPI_ASSIGN_OR_RETURN(int curl_code, api_->curl_easy_perform(curl_.get()));
  if (curl_code != curl::CURLE_OK) {
    return absl::UnavailableError(absl::StrCat(
        "curl_easy_perform returned with the error code ", curl_code));
  }

  // Get pointer to the memory chunk
  SAPI_RETURN_IF_ERROR(sandbox_->TransferFromSandboxee(chunk_.get()));
  return std::string(reinterpret_cast<char*>(chunk_->GetData()));
}

namespace {

// Read the socket until str is completely read
std::string ReadUntil(const int socket, const std::string& str,
                      const size_t max_request_size) {
  std::string str_read;
  str_read.reserve(max_request_size);

  // Read one char at a time until str is suffix of buf
  while (!absl::EndsWith(str_read, str)) {
    char next_char;
    if (str_read.size() >= max_request_size ||
        read(socket, &next_char, 1) < 1) {
      return "";
    }
    str_read += next_char;
  }

  return str_read;
}

// Parse HTTP headers to return the Content-Length
ssize_t GetContentLength(const std::string& headers) {
  constexpr char kContentLength[] = "Content-Length: ";
  // Find the Content-Length header
  const auto length_header_start = headers.find(kContentLength);

  // There is no Content-Length field
  if (length_header_start == std::string::npos) {
    return 0;
  }

  // Find Content-Length string
  const auto length_start = length_header_start + strlen(kContentLength);
  const auto length_bytes = headers.find("\r\n", length_start) - length_start;

  // length_bytes exceeds maximum
  if (length_bytes >= 64) {
    return -1;
  }

  // Convert string to int and return
  return std::stoi(headers.substr(length_start, length_bytes));
}

// Read exactly content_bytes from the socket
std::string ReadExact(int socket, size_t content_bytes) {
  std::string str_read;
  str_read.reserve(content_bytes);

  // Read one char at a time until all chars are read
  while (str_read.size() < content_bytes) {
    char next_char;
    if (read(socket, &next_char, 1) < 1) {
      return "";
    }
    str_read += next_char;
  }

  return str_read;
}

// Listen on the socket and answer back to requests
void ServerLoop(int listening_socket, sockaddr_in socket_address) {
  socklen_t socket_address_size = sizeof(socket_address);

  // Listen on the socket (maximum 1 connection)
  if (listen(listening_socket, 1) == -1) {
    return;
  }

  // Keep accepting connections until the thread is terminated
  // (i.e. server_thread_ is assigned to a new thread or destroyed)
  for (;;) {
    // File descriptor to the connection socket
    // This blocks the thread until a connection is established
    int accepted_socket =
        accept(listening_socket, reinterpret_cast<sockaddr*>(&socket_address),
               reinterpret_cast<socklen_t*>(&socket_address_size));
    if (accepted_socket == -1) {
      return;
    }

    constexpr int kMaxRequestSize = 4096;

    // Read until the end of the headers
    std::string headers =
        ReadUntil(accepted_socket, "\r\n\r\n", kMaxRequestSize);

    if (headers == "") {
      close(accepted_socket);
      return;
    }

    // Get the length of the request content
    ssize_t content_length = GetContentLength(headers);
    if (content_length > kMaxRequestSize - headers.size() ||
        content_length < 0) {
      close(accepted_socket);
      return;
    }

    // Read the request content
    std::string content = ReadExact(accepted_socket, content_length);

    // Prepare a response for the request
    std::string http_response =
        "HTTP/1.1 200 OK\nContent-Type: text/plain\nContent-Length: ";

    if (headers.substr(0, 3) == "GET") {
      http_response += "2\r\n\r\nOK";

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
  }
}

}  // namespace

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
  if (bind(listening_socket, reinterpret_cast<sockaddr*>(&socket_address),
           socket_address_size) == -1) {
    return;
  }

  // Assign an available port to the socket address object
  if (getsockname(listening_socket,
                  reinterpret_cast<sockaddr*>(&socket_address),
                  &socket_address_size) == -1) {
    return;
  }

  // Get the port number
  port_ = ntohs(socket_address.sin_port);

  // Set server_thread_ operation to socket listening
  server_thread_ = std::thread(ServerLoop, listening_socket, socket_address);
}

}  // namespace curl::tests
