// Copyright 2019 Google LLC
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

#include "sandboxed_api/sandbox2/network_proxy/server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syscall.h>

#include <cerrno>
#include <cstring>
#include <memory>

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "sandboxed_api/util/fileops.h"

namespace sandbox2 {

namespace file_util = ::sapi::file_util;

NetworkProxyServer::NetworkProxyServer(int fd, AllowedHosts* allowed_hosts,
                                       pthread_t monitor_thread_id)
    : violation_occurred_(false),
      comms_(std::make_unique<Comms>(fd)),
      fatal_error_(false),
      monitor_thread_id_(monitor_thread_id),
      allowed_hosts_(allowed_hosts) {}

void NetworkProxyServer::ProcessConnectRequest() {
  std::vector<uint8_t> addr;
  if (!comms_->RecvBytes(&addr)) {
    fatal_error_ = true;
    return;
  }

  const struct sockaddr* saddr = reinterpret_cast<const sockaddr*>(addr.data());

  // Only IPv4 TCP and IPv6 TCP are supported.
  if (!((addr.size() == sizeof(sockaddr_in) && saddr->sa_family == AF_INET) ||
        (addr.size() == sizeof(sockaddr_in6) &&
         saddr->sa_family == AF_INET6))) {
    SendError(EINVAL);
    return;
  }

  if (!allowed_hosts_->IsHostAllowed(saddr)) {
    NotifyViolation(saddr);
    return;
  }

  int new_socket = socket(saddr->sa_family, SOCK_STREAM, 0);
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
  while (!fatal_error_ &&
         !violation_occurred_.load(std::memory_order_relaxed)) {
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

void NetworkProxyServer::NotifyViolation(const struct sockaddr* saddr) {
  if (absl::StatusOr<std::string> result = AddrToString(saddr); result.ok()) {
    violation_msg_ = std::move(result).value();
  } else {
    violation_msg_ = std::string(result.status().message());
  }
  violation_occurred_.store(true, std::memory_order_release);
  pthread_kill(monitor_thread_id_, SIGCHLD);
}

}  // namespace sandbox2
