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

#include "sandboxed_api/sandbox2/network_proxy/client.h"

#include <sys/socket.h>
#include <syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "sandboxed_api/sandbox2/util/syscall_trap.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/status_macros.h"

namespace sandbox2 {
using ::sapi::file_util::fileops::FDCloser;

absl::Status NetworkProxyClient::Connect(int sockfd,
                                         const struct sockaddr* addr,
                                         socklen_t addrlen) {
  // Check if socket is SOCK_STREAM
  int type;
  socklen_t type_size = sizeof(int);
  int result = getsockopt(sockfd, SOL_SOCKET, SO_TYPE, &type, &type_size);
  if (result == -1) {
    return absl::FailedPreconditionError("Invalid socket FD");
  }
  if (type_size != sizeof(int) || type != SOCK_STREAM) {
    errno = EINVAL;
    return absl::InvalidArgumentError(
        "Invalid socket, only SOCK_STREAM is allowed");
  }
  SAPI_ASSIGN_OR_RETURN(FDCloser s, ConnectInternal(addr, addrlen));
  if (dup2(s.get(), sockfd) == -1) {
    return absl::InternalError("Duplicating socket failed");
  }
  return absl::OkStatus();
}

absl::StatusOr<FDCloser> NetworkProxyClient::ConnectInternal(
    const struct sockaddr* addr, socklen_t addrlen) {
  absl::MutexLock lock(&mutex_);
  // Send sockaddr struct
  if (!comms_.SendBytes(reinterpret_cast<const uint8_t*>(addr), addrlen)) {
    errno = EIO;
    return absl::InternalError("Sending data to network proxy failed");
  }

  int result;
  if (!comms_.RecvInt32(&result)) {
    errno = EIO;
    return absl::InternalError("Receiving data from the network proxy failed");
  }
  if (result != 0) {
    errno = result;
    return absl::ErrnoToStatus(errno, "Error in network proxy server");
  }

  int sock;
  if (!comms_.RecvFD(&sock)) {
    errno = EIO;
    return absl::InternalError("Receiving fd from network proxy failed");
  }
  return FDCloser(sock);
}

NetworkProxyClient* NetworkProxyHandler::network_proxy_client_ = nullptr;

absl::Status NetworkProxyHandler::InstallNetworkProxyHandler(
    NetworkProxyClient* npc) {
  if (network_proxy_client_ != nullptr) {
    return absl::AlreadyExistsError(
        "Network proxy handler is already installed");
  }
  network_proxy_client_ = npc;
  if (!SyscallTrap::Install([](int nr, SyscallTrap::Args args, uintptr_t* rv) {
        return ProcessSeccompTrap(nr, args, rv);
      })) {
    return absl::InternalError("Could not install syscall trap");
  }
  return absl::OkStatus();
}

bool NetworkProxyHandler::ProcessSeccompTrap(int nr, SyscallTrap::Args args,
                                             uintptr_t* rv) {
  int sockfd;
  const struct sockaddr* addr;
  socklen_t addrlen;

  if (nr == __NR_connect) {
    sockfd = static_cast<int>(args[0]);
    addr = reinterpret_cast<const struct sockaddr*>(args[1]);
    addrlen = static_cast<socklen_t>(args[2]);
#if defined(SAPI_PPC64_LE)
  } else if (nr == __NR_socketcall &&
             static_cast<int>(args[0]) == SYS_CONNECT) {
    auto* connect_args = reinterpret_cast<uint64_t*>(args[1]);
    sockfd = static_cast<int>(connect_args[0]);
    addr = reinterpret_cast<const struct sockaddr*>(connect_args[1]);
    addrlen = static_cast<socklen_t>(connect_args[2]);
#endif
  } else {
    return false;
  }

  absl::Status result = network_proxy_client_->Connect(sockfd, addr, addrlen);
  if (result.ok()) {
    *rv = 0;
  } else {
    *rv = -errno;
  }
  return true;
}

}  // namespace sandbox2
