// Copyright 2023 Google LLC
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

// A binary doing various malloc calls to check that the malloc policy works as
// expected.
// This file is an example of a network sandboxed binary inside a network
// namespace. It can't connect with the server directly, but the executor can
// establish a connection and pass the connected socket to the sandboxee.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <variant>

#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/network_proxy/client.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/status_macros.h"

ABSL_FLAG(bool, connect_with_handler, true, "Connect using automatic mode.");
ABSL_FLAG(bool, ipv6, false, "Use IPv6.");

namespace {

using ::sapi::file_util::fileops::FDCloser;

struct IPAddr {
  size_t GetSize() const {
    return addr.index() == 0 ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
  }
  const sockaddr* GetPtr() const {
    return addr.index() == 0
               ? reinterpret_cast<const sockaddr*>(&std::get<0>(addr))
               : reinterpret_cast<const sockaddr*>(&std::get<1>(addr));
  }
  std::variant<sockaddr_in, sockaddr_in6> addr;
};

static sandbox2::NetworkProxyClient* g_proxy_client;

ssize_t ReadFromFd(int fd, uint8_t* buf, size_t size) {
  ssize_t received = 0;
  while (received < size) {
    ssize_t read_status =
        TEMP_FAILURE_RETRY(read(fd, &buf[received], size - received));
    if (read_status == 0) {
      break;
    }
    if (read_status < 0) {
      return -1;
    }
    received += read_status;
  }
  return received;
}

absl::Status CommunicationTest(int sock) {
  char received[1025] = {0};

  if (ReadFromFd(sock, reinterpret_cast<uint8_t*>(received),
                 sizeof(received) - 1) <= 0) {
    return absl::InternalError("Data receiving error");
  }
  absl::PrintF("Sandboxee received data from the server:\n\n%s\n", received);
  if (strcmp(received, "Hello World\n")) {
    return absl::InternalError("Data receiving error");
  }
  return absl::OkStatus();
}

IPAddr CreateAddress(int port) {
  static struct sockaddr_in saddr4 {};
  static struct sockaddr_in6 saddr6 {};
  saddr4.sin_family = AF_INET;
  saddr6.sin6_family = AF_INET6;
  saddr4.sin_port = saddr6.sin6_port = htons(port);
  PCHECK(inet_pton(AF_INET6, "::1", &saddr6.sin6_addr) == 1);
  PCHECK(inet_pton(AF_INET, "127.0.0.1", &saddr4.sin_addr) == 1);
  return absl::GetFlag(FLAGS_ipv6) ? IPAddr{.addr = saddr6}
                                   : IPAddr{.addr = saddr4};
}

absl::Status ConnectWithoutHandler(int s, IPAddr saddr) {
  return g_proxy_client->Connect(s, saddr.GetPtr(), saddr.GetSize());
}

absl::Status ConnectWithHandler(int s, IPAddr saddr) {
  int err = connect(s, saddr.GetPtr(), saddr.GetSize());
  if (err != 0) {
    return absl::InternalError("connect() failed");
  }

  return absl::OkStatus();
}

absl::StatusOr<FDCloser> ConnectToServer(int port) {
  IPAddr addr = CreateAddress(port);

  FDCloser s(
      socket(absl::GetFlag(FLAGS_ipv6) ? AF_INET6 : AF_INET, SOCK_STREAM, 0));
  if (s.get() < 0) {
    return absl::ErrnoToStatus(errno, "socket()");
  }

  if (absl::GetFlag(FLAGS_connect_with_handler)) {
    SAPI_RETURN_IF_ERROR(ConnectWithHandler(s.get(), addr));
  } else {
    SAPI_RETURN_IF_ERROR(ConnectWithoutHandler(s.get(), addr));
  }

  LOG(INFO) << "Connected to the server";
  return s;
}

}  // namespace

int main(int argc, char* argv[]) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  // Set-up the sandbox2::Client object, using a file descriptor (1023).
  sandbox2::Comms comms(sandbox2::Comms::kDefaultConnection);
  sandbox2::Client sandbox2_client(&comms);

  if (absl::GetFlag(FLAGS_connect_with_handler)) {
    if (auto status = sandbox2_client.InstallNetworkProxyHandler();
        !status.ok()) {
      LOG(ERROR) << "InstallNetworkProxyHandler() failed: " << status;
      return 1;
    }
  } else {
    g_proxy_client = sandbox2_client.GetNetworkProxyClient();
  }

  // Receive port number of the server
  int port;
  if (!comms.RecvInt32(&port)) {
    LOG(ERROR) << "Failed to receive port number";
    return 2;
  }

  absl::StatusOr<FDCloser> client = ConnectToServer(port);
  if (!client.ok()) {
    LOG(ERROR) << client.status();
    return 3;
  }

  if (auto status = CommunicationTest(client->get()); !status.ok()) {
    LOG(ERROR) << status;
    return 4;
  }
  return 0;
}
