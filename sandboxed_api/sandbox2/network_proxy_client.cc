// Copyright 2019 Google LLC. All Rights Reserved.
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

#include "sandboxed_api/sandbox2/network_proxy_client.h"

#include <cerrno>
#include <iostream>

#include <glog/logging.h>
#include "absl/strings/str_cat.h"
#include "sandboxed_api/sandbox2/util/strerror.h"
#include "sandboxed_api/util/canonical_errors.h"
#include "sandboxed_api/util/status.h"
#include "sandboxed_api/util/status_macros.h"

namespace sandbox2 {

constexpr char NetworkProxyClient::kFDName[];

int NetworkProxyClient::ConnectHandler(int sockfd, const struct sockaddr* addr,
                                       socklen_t addrlen) {
  sapi::Status status = Connect(sockfd, addr, addrlen);
  if (status.ok()) {
    return 0;
  }
  PLOG(ERROR) << "ConnectHandler() failed: " << status.message();
  return -1;
}

sapi::Status NetworkProxyClient::Connect(int sockfd,
                                         const struct sockaddr* addr,
                                         socklen_t addrlen) {
  absl::MutexLock lock(&mutex_);

  // Check if socket is SOCK_STREAM
  int type;
  socklen_t type_size = sizeof(int);
  int result = getsockopt(sockfd, SOL_SOCKET, SO_TYPE, &type, &type_size);
  if (result == -1) {
    return sapi::FailedPreconditionError("Invalid socket FD");
  }
  if (type_size != sizeof(int) || type != SOCK_STREAM) {
    errno = EINVAL;
    return sapi::InvalidArgumentError(
        "Invalid socket, only SOCK_STREAM is allowed");
  }

  // Send sockaddr struct
  if (!comms_.SendBytes(reinterpret_cast<const uint8_t*>(addr), addrlen)) {
    errno = EIO;
    return sapi::InternalError("Sending data to network proxy failed");
  }

  SAPI_RETURN_IF_ERROR(ReceiveRemoteResult());

  // Receive new socket
  int s;
  if (!comms_.RecvFD(&s)) {
    errno = EIO;
    return sapi::InternalError("Receiving data from network proxy failed");
  }
  if (dup2(s, sockfd) == -1) {
    close(s);
    return sapi::InternalError("Processing data from network proxy failed");
  }
  return sapi::OkStatus();
}

sapi::Status NetworkProxyClient::ReceiveRemoteResult() {
  int result;
  if (!comms_.RecvInt32(&result)) {
    errno = EIO;
    return sapi::InternalError("Receiving data from the network proxy failed");
  }
  if (result != 0) {
    errno = result;
    return sapi::InternalError(
        absl::StrCat("Error in network proxy: ", StrError(errno)));
  }
  return sapi::OkStatus();
}

}  // namespace sandbox2
