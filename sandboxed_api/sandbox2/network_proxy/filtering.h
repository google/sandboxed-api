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

#ifndef SANDBOXED_API_SANDBOX2_NETWORK_PROXY_FILTERING_H_
#define SANDBOXED_API_SANDBOX2_NETWORK_PROXY_FILTERING_H_

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/comms.h"

namespace sandbox2 {

// Converts sockaddr_in, sockaddr_in6, or sockaddr_un structure into a string
// representation.
absl::StatusOr<std::string> AddrToString(
    const struct sockaddr* saddr,
    socklen_t len = sizeof(struct sockaddr_storage));

struct IPv4 {
  in_addr_t ip;
  in_addr_t mask;
  uint32_t port;
  IPv4(in_addr_t IP, in_addr_t mask, uint32_t port)
      : ip(IP), mask(mask), port(port) {}
};

struct IPv6 {
  in6_addr ip;
  in6_addr mask;
  uint32_t port;
  IPv6(in6_addr IP, in6_addr mask, uint32_t port)
      : ip(IP), mask(mask), port(port) {}
};

// Keeps a list of allowed pairs of IP, mask, port, and UNIX socket paths.
class AllowedEndpoints {
 public:
  // ip_and_mask should have one of following formats: IP, IP/mask, IP/cidr.
  absl::Status AllowIPv4(const std::string& ip_and_mask, uint32_t port = 0);
  // ip_and_mask should have following format: IP or IP/cidr.
  absl::Status AllowIPv6(const std::string& ip_and_mask, uint32_t port = 0);
  // Allows connections to a pathname or abstract UNIX domain socket at path.
  absl::Status AllowUnixSocket(absl::string_view path);
  // Enables or disables UNIX domain socket filtering.
  void set_filter_unix_sockets(bool enable) { filter_unix_sockets_ = enable; }
  bool filter_unix_sockets() const { return filter_unix_sockets_; }
  // Canonicalizes a UNIX domain socket path by resolving existing parent
  // directories.
  static std::string CanonicalizeSocketPath(absl::string_view path);
  // Checks if this endpoint is allowed. If canonical_path is provided and the
  // endpoint is a UNIX domain path socket, sets *canonical_path to the
  // canonicalized socket path.
  bool IsEndpointAllowed(const struct sockaddr* saddr,
                         socklen_t len = sizeof(struct sockaddr),
                         std::string* canonical_path = nullptr) const;

 private:
  absl::Status AllowIPv4(const std::string& ip, const std::string& mask,
                         uint32_t cidr, uint32_t port);
  absl::Status AllowIPv6(const std::string& ip, uint32_t cidr, uint32_t port);
  bool IsIPv4Allowed(const struct sockaddr_in* saddr) const;
  bool IsIPv6Allowed(const struct sockaddr_in6* saddr) const;
  bool IsUnixSocketAllowed(const struct sockaddr_un* saddr, socklen_t len,
                           std::string* canonical_path = nullptr) const;

  std::vector<IPv4> allowed_IPv4_;
  std::vector<IPv6> allowed_IPv6_;
  std::vector<std::string> allowed_unix_paths_;
  bool filter_unix_sockets_ = false;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_NETWORK_PROXY_FILTERING_H_
