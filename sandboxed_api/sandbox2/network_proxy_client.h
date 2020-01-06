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

#ifndef SANDBOXED_API_SANDBOX2_NETWORK_PROXY_CLIENT_H_
#define SANDBOXED_API_SANDBOX2_NETWORK_PROXY_CLIENT_H_

#include <netinet/in.h>

#include "absl/synchronization/mutex.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/util/status.h"

namespace sandbox2 {

class NetworkProxyClient {
 public:
  static constexpr char kFDName[] = "sb2_networkproxy";

  explicit NetworkProxyClient(int fd) : comms_(fd) {}

  NetworkProxyClient(const NetworkProxyClient&) = delete;
  NetworkProxyClient& operator=(const NetworkProxyClient&) = delete;

  // Establishes a new network connection.
  // Semantic is similar to a regular connect() call.
  // Arguments are sent to network proxy server, which sends back a connected
  // socket.
  sapi::Status Connect(int sockfd, const struct sockaddr* addr,
                       socklen_t addrlen);
  // Same as Connect, but with same API as regular connect() call.
  int ConnectHandler(int sockfd, const struct sockaddr* addr,
                     socklen_t addrlen);

 private:
  Comms comms_;
  sapi::Status ReceiveRemoteResult();

  // Needed to make the Proxy thread safe.
  absl::Mutex mutex_;
};

class NetworkProxyHandler {
 public:
  // Installs the handler that redirects connect() syscalls to the trap
  // function. This function exchange data with NetworkProxyServer that checks
  // if this connection is allowed and sends the connected socket to us.
  // In other words, this function just use NetworkProxyClient class.
  static sapi::Status InstallNetworkProxyHandler(NetworkProxyClient* npc);
  void ProcessSeccompTrap(int nr, siginfo_t* info, void* void_context);

 private:
  NetworkProxyHandler(NetworkProxyClient* npc) : network_proxy_client_(npc) {
    InstallSeccompTrap();
  }
  void InvokeOldAct(int nr, siginfo_t* info, void* void_context);
  void InstallSeccompTrap();

  struct sigaction oldact_;
  NetworkProxyClient* network_proxy_client_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_NETWORK_PROXY_CLIENT_H_
