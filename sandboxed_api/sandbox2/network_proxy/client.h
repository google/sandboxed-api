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

#ifndef SANDBOXED_API_SANDBOX2_NETWORK_PROXY_CLIENT_H_
#define SANDBOXED_API_SANDBOX2_NETWORK_PROXY_CLIENT_H_

#include <sys/socket.h>

#include <cstdint>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/util/syscall_trap.h"
#include "sandboxed_api/util/fileops.h"

namespace sandbox2 {

class NetworkProxyClient {
 public:
  static constexpr char kFDName[] = "sb2_networkproxy";

  explicit NetworkProxyClient(int fd) : comms_(fd) {}

  NetworkProxyClient(const NetworkProxyClient&) = delete;
  NetworkProxyClient& operator=(const NetworkProxyClient&) = delete;

  // Establishes a new network connection with semantics similar to a regular
  // connect() call. Arguments are sent to network proxy server, which sends
  // back a connected socket.
  absl::Status Connect(int sockfd, const struct sockaddr* addr,
                       socklen_t addrlen);

 private:
  absl::StatusOr<sapi::file_util::fileops::FDCloser> ConnectInternal(
      const struct sockaddr* addr, socklen_t addrlen);

  // Needed to make the Proxy thread safe.
  absl::Mutex mutex_;
  Comms comms_ ABSL_GUARDED_BY(mutex_);
};

class NetworkProxyHandler {
 public:
  // Installs the handler that redirects connect() syscalls to the trap
  // function. This function exchanges data with NetworkProxyServer that checks
  // if this connection is allowed and sends the connected socket to us.
  static absl::Status InstallNetworkProxyHandler(NetworkProxyClient* npc);

  static bool ProcessSeccompTrap(int nr, SyscallTrap::Args args, uintptr_t* rv);

  static NetworkProxyClient* network_proxy_client_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_NETWORK_PROXY_CLIENT_H_
