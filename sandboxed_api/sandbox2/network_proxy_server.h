// Copyright 2019 Google LLC
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

#ifndef SANDBOXED_API_SANDBOX2_NETWORK_PROXY_SERVER_H_
#define SANDBOXED_API_SANDBOX2_NETWORK_PROXY_SERVER_H_

#include <memory>

#include "sandboxed_api/sandbox2/comms.h"

namespace sandbox2 {

// This is a proxy server that spawns connected sockets on requests.
// Then it sends the file descriptor to the requestor. It is used to get around
// limitations created by network namespaces.
class NetworkProxyServer {
 public:
  explicit NetworkProxyServer(int fd);

  NetworkProxyServer(const NetworkProxyServer&) = delete;
  NetworkProxyServer& operator=(const NetworkProxyServer&) = delete;

  // Starts handling incoming connection requests.
  void Run();

 private:
  // Notifies the network proxy client about the error and sends its code.
  void SendError(int saved_errno);

  // Notifies the network proxy client that no error occurred.
  void NotifySuccess();

  // Serves connection requests from the network proxy client.
  void ProcessConnectRequest();

  std::unique_ptr<Comms> comms_;
  bool fatal_error_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_NETWORK_PROXY_SERVER_H_
