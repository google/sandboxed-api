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

#include "sandboxed_api/sandbox2/network_proxy/testing.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "absl/base/macros.h"
#include "absl/log/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/status_macros.h"
#include "sandboxed_api/util/thread.h"

namespace sandbox2 {
namespace {

using sapi::file_util::fileops::FDCloser;

absl::StatusOr<FDCloser> CreateServerSocket(int port, bool ipv6 = true) {
  // Listen to localhost only
  sockaddr_in6 addr6;
  sockaddr_in addr4;
  addr6.sin6_family = AF_INET6;
  addr4.sin_family = AF_INET;
  addr4.sin_port = addr6.sin6_port = htons(port);
  PCHECK(inet_pton(AF_INET, "127.0.0.1", &addr4.sin_addr.s_addr) == 1);
  PCHECK(inet_pton(AF_INET6, "::1", &addr6.sin6_addr.s6_addr) == 1);
  sockaddr* addr = ipv6 ? reinterpret_cast<sockaddr*>(&addr6)
                        : reinterpret_cast<sockaddr*>(&addr4);
  size_t addr_size = ipv6 ? sizeof(addr6) : sizeof(addr4);
  sapi::file_util::fileops::FDCloser s(
      socket(addr->sa_family, SOCK_STREAM | SOCK_CLOEXEC, 0));
  if (s.get() < 0) {
    return absl::InternalError("socket() failed");
  }
  int enable = 1;
  if (setsockopt(s.get(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) <
      0) {
    return absl::InternalError("setsockopt() failed");
  }
  if (bind(s.get(), addr, addr_size) < 0) {
    return absl::InternalError("bind() failed");
  }
  if (listen(s.get(), 1) < 0) {
    return absl::InternalError("listen() failed");
  }
  return s;
}

}  // namespace

absl::StatusOr<std::unique_ptr<NetworkProxyTestServer>>
NetworkProxyTestServer::Start(bool ipv6) {
   static int port = 8085;
  FDCloser event_fd(eventfd(0, 0));
  if (event_fd.get() < 0) {
    return absl::InternalError("eventfd() failed");
  }
  SAPI_ASSIGN_OR_RETURN(FDCloser server_socket, CreateServerSocket(port, ipv6));
  auto server = absl::WrapUnique(new NetworkProxyTestServer(
      port, std::move(server_socket), std::move(event_fd)));
  server->Spawn();
  return server;
}

void NetworkProxyTestServer::Stop() {
  if (event_fd_.get() < 0) {
    return;
  }
  uint64_t value = 1;
  PCHECK(write(event_fd_.get(), &value, sizeof(value)) == sizeof(value));
  thread_.Join();
  event_fd_.Close();
  server_socket_.Close();
}

void NetworkProxyTestServer::Run() {
  struct pollfd pfds[] = {
      {.fd = server_socket_.get(), .events = POLLIN},
      {.fd = event_fd_.get(), .events = POLLIN},
  };
  do {
    PCHECK(poll(pfds, ABSL_ARRAYSIZE(pfds), -1) > 0);
    if (pfds[1].revents & POLLIN) {
      return;
    }
  } while (!(pfds[0].revents & POLLIN));
  FDCloser client(accept(server_socket_.get(), 0, 0));
  PCHECK(client.get() >= 0);
  constexpr absl::string_view kMsg = "Hello World\n";
  PCHECK(write(client.get(), kMsg.data(), kMsg.size()) == kMsg.size());
}

void NetworkProxyTestServer::Spawn() {
  thread_ = sapi::Thread(this, &NetworkProxyTestServer::Run,
                         "NetworkProxyTestServerThread");
}

}  // namespace sandbox2
