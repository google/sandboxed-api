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

#include "sandboxed_api/sandbox2/network_proxy/filtering.h"

#include <arpa/inet.h>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "sandboxed_api/util/status_macros.h"

namespace sandbox2 {

static absl::StatusOr<std::string> Addr6ToString(
    const struct sockaddr_in6* saddr) {
  char addr[INET6_ADDRSTRLEN];
  int port = htons(saddr->sin6_port);
  if (!inet_ntop(AF_INET6, &saddr->sin6_addr, addr, sizeof addr)) {
    return absl::InternalError(
        "Error in converting sockaddr_in6 addres to string");
  }
  return absl::StrCat("IP: ", addr, ", port: ", port);
}

// Converts sockaddr_in structure into a string IPv4 representation.
static absl::StatusOr<std::string> Addr4ToString(
    const struct sockaddr_in* saddr) {
  char addr[INET_ADDRSTRLEN];
  int port = htons(saddr->sin_port);
  if (!inet_ntop(AF_INET, &saddr->sin_addr, addr, sizeof addr)) {
    return absl::InternalError(
        "Error in converting sockaddr_in addres to string");
  }
  return absl::StrCat("IP: ", addr, ", port: ", port);
}

// Converts sockaddr_in6 structure into a string IPv6 representation.
absl::StatusOr<std::string> AddrToString(const struct sockaddr* saddr) {
  switch (saddr->sa_family) {
    case AF_INET:
      return Addr4ToString(reinterpret_cast<const struct sockaddr_in*>(saddr));
    case AF_INET6:
      return Addr6ToString(reinterpret_cast<const struct sockaddr_in6*>(saddr));
    default:
      return absl::InternalError(
          absl::StrCat("Unexpected sa_family value: ", saddr->sa_family));
  }
}

static absl::Status IPStringToAddr(const std::string& ip, int address_family,
                                   void* addr) {
  if (int err = inet_pton(address_family, ip.c_str(), addr); err == 0) {
    return absl::InvalidArgumentError(absl::StrCat("Invalid address: ", ip));
  } else if (err == -1) {
    return absl::ErrnoToStatus(errno,
                               absl::StrCat("inet_pton() failed for ", ip));
  }
  return absl::OkStatus();
}

// Parses a string of type IP or IP/mask or IP/cidr and saves appropriate
// values in output arguments.
static absl::Status ParseIpAndMask(const std::string& ip_and_mask,
                                   std::string* ip, std::string* mask,
                                   uint32_t* cidr) {
  // mask is checked later because only IPv4 format supports mask
  if (ip == nullptr || cidr == nullptr) {
    return absl::InvalidArgumentError(
        "ip and cidr arguments of ParseIpAndMask cannot be nullptr");
  }
  *cidr = 0;

  std::vector<std::string> ip_and_mask_split =
      absl::StrSplit(ip_and_mask, absl::MaxSplits('/', 1));

  *ip = ip_and_mask_split[0];
  if (ip_and_mask_split.size() == 1) {
    return absl::OkStatus();
  }
  std::string mask_or_cidr = ip_and_mask_split[1];

  const bool has_dot = !absl::StrContains(mask_or_cidr, '.');
  if (has_dot) {  // mask_or_cidr is cidr
    bool res = absl::SimpleAtoi<uint32_t>(mask_or_cidr, cidr);
    if (!res || !*cidr) {
      return absl::InvalidArgumentError(
          absl::StrCat(mask_or_cidr, " is not a correct cidr"));
    }
  } else {
    if (mask == nullptr) {
      return absl::InvalidArgumentError(
          "mask argument of ParseIpAndMask cannot be NULL in this case");
    }
    *mask = std::string(mask_or_cidr);
  }
  return absl::OkStatus();
}

static absl::Status CidrToIn6Addr(uint32_t cidr, in6_addr* addr) {
  if (cidr > 128) {
    return absl::InvalidArgumentError(
        absl::StrCat(cidr, " is not a correct cidr"));
  }

  memset(addr, 0, sizeof(*addr));

  int i = 0;
  while (cidr >= 8) {
    addr->s6_addr[i++] = 0xff;
    cidr -= 8;
  }
  if (cidr) {
    uint8_t tmp = 0x0;
    while (cidr--) {
      tmp >>= 1;
      tmp |= 0x80;
    }
    addr->s6_addr[i] = tmp;
  }
  return absl::OkStatus();
}

static absl::Status CidrToInAddr(uint32_t cidr, in_addr* addr) {
  if (cidr > 32) {
    return absl::InvalidArgumentError(
        absl::StrCat(cidr, " is not a correct cidr"));
  }

  memset(addr, 0, sizeof(*addr));

  uint32_t tmp = 0x0;
  while (cidr--) {
    tmp >>= 1;
    tmp |= 0x80000000;
  }
  addr->s_addr = htonl(tmp);
  return absl::OkStatus();
}

static bool IsIPv4MaskCorrect(in_addr_t m) {
  m = ntohl(m);
  if (m == 0) {
    return false;
  }
  m = ~m + 1;
  return !(m & (m - 1));
}

absl::Status AllowedHosts::AllowIPv4(const std::string& ip_and_mask,
                                     uint32_t port) {
  std::string ip, mask;
  uint32_t cidr;
  SAPI_RETURN_IF_ERROR(ParseIpAndMask(ip_and_mask, &ip, &mask, &cidr));
  SAPI_RETURN_IF_ERROR(AllowIPv4(ip, mask, cidr, port));

  return absl::OkStatus();
}

absl::Status AllowedHosts::AllowIPv6(const std::string& ip_and_mask,
                                     uint32_t port) {
  std::string ip;
  uint32_t cidr;
  SAPI_RETURN_IF_ERROR(ParseIpAndMask(ip_and_mask, &ip, NULL, &cidr));
  SAPI_RETURN_IF_ERROR(AllowIPv6(ip, cidr, port));
  return absl::OkStatus();
}

absl::Status AllowedHosts::AllowIPv4(const std::string& ip,
                                     const std::string& mask, uint32_t cidr,
                                     uint32_t port) {
  in_addr addr{};
  in_addr m{};

  if (mask.length()) {
    SAPI_RETURN_IF_ERROR(IPStringToAddr(mask, AF_INET, &m));

    if (!IsIPv4MaskCorrect(m.s_addr)) {
      return absl::InvalidArgumentError(
          absl::StrCat(mask, " is not a correct mask"));
    }

  } else {
    if (cidr > 32) {
      return absl::InvalidArgumentError(
          absl::StrCat(cidr, " is not a correct cidr"));
    }
    if (!cidr) {
      cidr = 32;
    }

    SAPI_RETURN_IF_ERROR(CidrToInAddr(cidr, &m));
  }

  SAPI_RETURN_IF_ERROR(IPStringToAddr(ip, AF_INET, &addr));
  allowed_IPv4_.emplace_back(addr.s_addr, m.s_addr, htons(port));

  return absl::OkStatus();
}

absl::Status AllowedHosts::AllowIPv6(const std::string& ip, uint32_t cidr,
                                     uint32_t port) {
  if (cidr == 0) {
    cidr = 128;
  }

  in6_addr addr{};
  SAPI_RETURN_IF_ERROR(IPStringToAddr(ip, AF_INET6, &addr));

  in6_addr m;
  SAPI_RETURN_IF_ERROR(CidrToIn6Addr(cidr, &m));

  allowed_IPv6_.emplace_back(addr, m, htons(port));
  return absl::OkStatus();
}

bool AllowedHosts::IsHostAllowed(const struct sockaddr* saddr) const {
  switch (saddr->sa_family) {
    case AF_INET:
      return IsIPv4Allowed(reinterpret_cast<const struct sockaddr_in*>(saddr));
    case AF_INET6:
      return IsIPv6Allowed(reinterpret_cast<const struct sockaddr_in6*>(saddr));
    default:
      LOG(FATAL) << absl::StrCat("Unexpected sa_family value: ",
                                 saddr->sa_family);
      return false;
  }
}

bool AllowedHosts::IsIPv6Allowed(const struct sockaddr_in6* saddr) const {
  auto result = std::find_if(
      allowed_IPv6_.begin(), allowed_IPv6_.end(), [saddr](const IPv6& entry) {
        for (int i = 0; i < 16; i++) {
          if ((entry.ip.s6_addr[i] & entry.mask.s6_addr[i]) !=
              (saddr->sin6_addr.s6_addr[i] & entry.mask.s6_addr[i])) {
            return false;
          }
        }
        if (!entry.port || (entry.port == saddr->sin6_port)) {
          return true;
        }
        return false;
      });

  return result != allowed_IPv6_.end();
}

bool AllowedHosts::IsIPv4Allowed(const struct sockaddr_in* saddr) const {
  auto result = std::find_if(
      allowed_IPv4_.begin(), allowed_IPv4_.end(), [saddr](const IPv4& entry) {
        return ((entry.ip & entry.mask) ==
                (saddr->sin_addr.s_addr & entry.mask)) &&
               (!entry.port || (entry.port == saddr->sin_port));
      });

  return result != allowed_IPv4_.end();
}

}  // namespace sandbox2
