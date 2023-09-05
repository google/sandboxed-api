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

#ifndef SANDBOXED_API_SANDBOX2_EXAMPLES_NETWORK_PROXY_NETWORKPROXY_LIB_H_
#define SANDBOXED_API_SANDBOX2_EXAMPLES_NETWORK_PROXY_NETWORKPROXY_LIB_H_

#include <memory>
#include <thread>
#include <utility>

#include "absl/status/statusor.h"
#include "sandboxed_api/util/fileops.h"

namespace sandbox2 {

class NetworkProxyTestServer {
 public:
  static absl::StatusOr<std::unique_ptr<NetworkProxyTestServer>> Start(
      bool ipv6 = true);

  NetworkProxyTestServer(NetworkProxyTestServer&&) = delete;
  NetworkProxyTestServer& operator=(NetworkProxyTestServer&&) = delete;

  ~NetworkProxyTestServer() { Stop(); }

  int port() { return port_; }
  void Stop();

 private:
  NetworkProxyTestServer(int port,
                         sapi::file_util::fileops::FDCloser server_socket,
                         sapi::file_util::fileops::FDCloser event_fd)
      : port_(port),
        server_socket_(std::move(server_socket)),
        event_fd_(std::move(event_fd)) {}
  void Spawn();
  void Run();
  std::thread thread_;
  int port_;
  sapi::file_util::fileops::FDCloser server_socket_;
  sapi::file_util::fileops::FDCloser event_fd_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_EXAMPLES_NETWORK_PROXY_NETWORKPROXY_LIB_H_
