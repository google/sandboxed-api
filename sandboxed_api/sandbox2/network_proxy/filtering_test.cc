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
#include <linux/unistd.h>
#include <string.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sandbox2 {
namespace {

using ::sapi::IsOk;
using ::testing::IsFalse;
using ::testing::IsTrue;

static struct sockaddr* PrepareIpv6(const std::string& ip, uint32_t port = 80) {
  static struct sockaddr_in6 saddr {};
  memset(&saddr, 0, sizeof(saddr));

  saddr.sin6_family = AF_INET6;
  saddr.sin6_port = htons(port);

  int err = inet_pton(AF_INET6, ip.c_str(), &saddr.sin6_addr);
  CHECK_GE(err, -1);

  return reinterpret_cast<struct sockaddr*>(&saddr);
}

static struct sockaddr* PrepareIpv4(const std::string& ip, uint32_t port = 80) {
  static struct sockaddr_in saddr {};
  memset(&saddr, 0, sizeof(saddr));

  saddr.sin_family = AF_INET;
  saddr.sin_port = htons(port);

  int err = inet_pton(AF_INET, ip.c_str(), &saddr.sin_addr);
  CHECK_GE(err, -1);

  return reinterpret_cast<struct sockaddr*>(&saddr);
}

TEST(FilteringTest, Basic) {
  sandbox2::AllowedHosts allowed_hosts;

  // Create rules
  EXPECT_THAT(allowed_hosts.AllowIPv4("127.0.0.1"), IsOk());
  EXPECT_THAT(allowed_hosts.AllowIPv4("127.0.0.2", 33), IsOk());
  EXPECT_THAT(allowed_hosts.AllowIPv4("120.120.120.120/255.255.255.0"), IsOk());
  EXPECT_THAT(allowed_hosts.AllowIPv4("130.130.130.130/255.255.252.0", 1000),
              IsOk());
  EXPECT_THAT(allowed_hosts.AllowIPv4("140.140.140.140/8"), IsOk());
  EXPECT_THAT(allowed_hosts.AllowIPv4("150.150.150.150/10", 123), IsOk());

  EXPECT_THAT(allowed_hosts.AllowIPv6("::2"), IsOk());
  EXPECT_THAT(allowed_hosts.AllowIPv6("::1", 80), IsOk());
  EXPECT_THAT(allowed_hosts.AllowIPv6("0:1234:0:0:0:0:0:0/32"), IsOk());
  EXPECT_THAT(allowed_hosts.AllowIPv6("0:5678:0:0:0:0:0:0/46", 70), IsOk());

  // IPv4 tests
  EXPECT_THAT(allowed_hosts.IsHostAllowed(PrepareIpv4("130.0.0.3")), IsFalse());
  EXPECT_THAT(allowed_hosts.IsHostAllowed(PrepareIpv4("127.0.0.1")), IsTrue());
  EXPECT_THAT(allowed_hosts.IsHostAllowed(PrepareIpv4("127.0.0.2")), IsFalse());
  EXPECT_THAT(allowed_hosts.IsHostAllowed(PrepareIpv4("127.0.0.2", 33)),
              IsTrue());
  EXPECT_THAT(allowed_hosts.IsHostAllowed(PrepareIpv4("120.120.120.255")),
              IsTrue());
  EXPECT_THAT(allowed_hosts.IsHostAllowed(PrepareIpv4("120.120.121.120")),
              IsFalse());
  EXPECT_THAT(allowed_hosts.IsHostAllowed(PrepareIpv4("130.130.128.130", 1000)),
              IsTrue());
  EXPECT_THAT(allowed_hosts.IsHostAllowed(PrepareIpv4("130.130.132.134", 1000)),
              IsFalse());
  EXPECT_THAT(allowed_hosts.IsHostAllowed(PrepareIpv4("130.130.128.130", 1001)),
              IsFalse());
  EXPECT_THAT(allowed_hosts.IsHostAllowed(PrepareIpv4("140.0.140.140")),
              IsTrue());
  EXPECT_THAT(allowed_hosts.IsHostAllowed(PrepareIpv4("141.140.140.140")),
              IsFalse());
  EXPECT_THAT(allowed_hosts.IsHostAllowed(PrepareIpv4("150.182.150.150", 123)),
              IsTrue());
  EXPECT_THAT(allowed_hosts.IsHostAllowed(PrepareIpv4("150.214.150.150", 123)),
              IsFalse());

  // IPv6 tests
  EXPECT_THAT(allowed_hosts.IsHostAllowed(PrepareIpv6("::3")), IsFalse());
  EXPECT_THAT(allowed_hosts.IsHostAllowed(PrepareIpv6("::2")), IsTrue());
  EXPECT_THAT(allowed_hosts.IsHostAllowed(PrepareIpv6("::1")), IsTrue());
  EXPECT_THAT(allowed_hosts.IsHostAllowed(PrepareIpv6("::1", 81)), IsFalse());
  EXPECT_THAT(allowed_hosts.IsHostAllowed(PrepareIpv6("0:1234:ffff:0:0:0:0:0")),
              IsTrue());
  EXPECT_THAT(allowed_hosts.IsHostAllowed(PrepareIpv6("0:1233:0000:0:0:0:0:0")),
              IsFalse());
  EXPECT_THAT(
      allowed_hosts.IsHostAllowed(PrepareIpv6("0:5678:0002:0:0:0:0:0", 70)),
      IsTrue());
  EXPECT_THAT(
      allowed_hosts.IsHostAllowed(PrepareIpv6("0:5678:0004:0:0:0:0:0", 70)),
      IsFalse());
  EXPECT_THAT(
      allowed_hosts.IsHostAllowed(PrepareIpv6("0:5678:0000:0:0:0:0:0", 2222)),
      IsFalse());
}

}  // namespace
}  // namespace sandbox2
