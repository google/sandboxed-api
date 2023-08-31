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
#include <sys/socket.h>

#include <thread>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/util/fileops.h"

namespace sandbox2 {
namespace {
void ServerThread(int port) {
  sapi::file_util::fileops::FDCloser s{
      socket(AF_INET6, SOCK_STREAM | SOCK_CLOEXEC, 0)};
  if (s.get() < 0) {
    PLOG(ERROR) << "socket() failed";
    return;
  }

  {
    int enable = 1;
    if (setsockopt(s.get(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) <
        0) {
      PLOG(ERROR) << "setsockopt(SO_REUSEADDR) failed";
      return;
    }
  }

  // Listen to localhost only.
  struct sockaddr_in6 addr = {};
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(port);

  int err = inet_pton(AF_INET6, "::1", &addr.sin6_addr.s6_addr);
  if (err == 0) {
    LOG(ERROR) << "inet_pton() failed";
    return;
  } else if (err == -1) {
    PLOG(ERROR) << "inet_pton() failed";
    return;
  }

  if (bind(s.get(), (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    PLOG(ERROR) << "bind() failed";
    return;
  }

  if (listen(s.get(), 1) < 0) {
    PLOG(ERROR) << "listen() failed";
    return;
  }

  sapi::file_util::fileops::FDCloser client{accept(s.get(), 0, 0)};
  if (client.get() < 0) {
    PLOG(ERROR) << "accept() failed";
    return;
  }

  constexpr absl::string_view kMsg = "Hello World\n";
  if (write(client.get(), kMsg.data(), kMsg.size()) < 0) {
    PLOG(ERROR) << "write() failed";
  }
}
}  // namespace

absl::StatusOr<int> StartNetworkProxyTestServer() {
  static int port = 8085;
  std::thread server_thread([] { ServerThread(++port); });
  server_thread.detach();
  return port;
}

}  // namespace sandbox2
