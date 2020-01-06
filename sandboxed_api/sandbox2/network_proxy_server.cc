// Copyright 2020 Google LLC. All Rights Reserved.
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

#include "sandboxed_api/sandbox2/network_proxy_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syscall.h>
#include <cerrno>
#include <cstring>

#include <glog/logging.h>
#include "absl/memory/memory.h"
#include "sandboxed_api/sandbox2/util/fileops.h"

namespace sandbox2 {

NetworkProxyServer::NetworkProxyServer(int fd)
    : comms_{absl::make_unique<Comms>(fd)}, fatal_error_{false} {}

void NetworkProxyServer::ProcessConnectRequest() {
  std::vector<uint8_t> addr;
  if (!comms_->RecvBytes(&addr)) {
    fatal_error_ = true;
    return;
  }

  const struct sockaddr_in* saddr =
      reinterpret_cast<const sockaddr_in*>(addr.data());

  // Only IPv4 TCP and IPv6 TCP are supported.
  if (!((addr.size() == sizeof(sockaddr_in) && saddr->sin_family == AF_INET) ||
        (addr.size() == sizeof(sockaddr_in6) &&
         saddr->sin_family == AF_INET6))) {
    SendError(EINVAL);
    return;
  }

  int new_socket = socket(saddr->sin_family, SOCK_STREAM, 0);
  if (new_socket < 0) {
    SendError(errno);
    return;
  }

  file_util::fileops::FDCloser new_socket_closer(new_socket);

  int result = connect(
      new_socket, reinterpret_cast<const sockaddr*>(addr.data()), addr.size());

  if (result == 0) {
    NotifySuccess();
    if (!fatal_error_) {
      if (!comms_->SendFD(new_socket)) {
        fatal_error_ = true;
        return;
      }
    }
  }
}

void NetworkProxyServer::Run() {
  while (!fatal_error_) {
    ProcessConnectRequest();
  }
  LOG(INFO)
      << "Clean shutdown or error occurred, shutting down NetworkProxyServer";
}

void NetworkProxyServer::SendError(int saved_errno) {
  if (!comms_->SendInt32(saved_errno)) {
    fatal_error_ = true;
  }
}

void NetworkProxyServer::NotifySuccess() {
  if (!comms_->SendInt32(0)) {
    fatal_error_ = true;
  }
}

}  // namespace sandbox2
