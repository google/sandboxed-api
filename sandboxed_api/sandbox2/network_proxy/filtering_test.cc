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
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/util/path.h"

namespace sandbox2 {
namespace {

using ::absl_testing::IsOk;
using ::testing::IsFalse;
using ::testing::IsTrue;

static struct sockaddr* PrepareIpv6(const std::string& ip, uint32_t port = 80) {
  static struct sockaddr_in6 saddr{};
  memset(&saddr, 0, sizeof(saddr));

  saddr.sin6_family = AF_INET6;
  saddr.sin6_port = htons(port);

  int err = inet_pton(AF_INET6, ip.c_str(), &saddr.sin6_addr);
  CHECK_GE(err, -1);

  return reinterpret_cast<struct sockaddr*>(&saddr);
}

static struct sockaddr* PrepareIpv4(const std::string& ip, uint32_t port = 80) {
  static struct sockaddr_in saddr{};
  memset(&saddr, 0, sizeof(saddr));

  saddr.sin_family = AF_INET;
  saddr.sin_port = htons(port);

  int err = inet_pton(AF_INET, ip.c_str(), &saddr.sin_addr);
  CHECK_GE(err, -1);

  return reinterpret_cast<struct sockaddr*>(&saddr);
}

TEST(FilteringTest, Basic) {
  sandbox2::AllowedEndpoints allowed_endpoints;

  // Create rules
  EXPECT_THAT(allowed_endpoints.AllowIPv4("127.0.0.1"), IsOk());
  EXPECT_THAT(allowed_endpoints.AllowIPv4("127.0.0.2", 33), IsOk());
  EXPECT_THAT(allowed_endpoints.AllowIPv4("120.120.120.120/255.255.255.0"),
              IsOk());
  EXPECT_THAT(
      allowed_endpoints.AllowIPv4("130.130.130.130/255.255.252.0", 1000),
      IsOk());
  EXPECT_THAT(allowed_endpoints.AllowIPv4("140.140.140.140/8"), IsOk());
  EXPECT_THAT(allowed_endpoints.AllowIPv4("150.150.150.150/10", 123), IsOk());

  EXPECT_THAT(allowed_endpoints.AllowIPv6("::2"), IsOk());
  EXPECT_THAT(allowed_endpoints.AllowIPv6("::1", 80), IsOk());
  EXPECT_THAT(allowed_endpoints.AllowIPv6("0:1234:0:0:0:0:0:0/32"), IsOk());
  EXPECT_THAT(allowed_endpoints.AllowIPv6("0:5678:0:0:0:0:0:0/46", 70), IsOk());

  // IPv4 tests
  EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(PrepareIpv4("130.0.0.3")),
              IsFalse());
  EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(PrepareIpv4("127.0.0.1")),
              IsTrue());
  EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(PrepareIpv4("127.0.0.2")),
              IsFalse());
  EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(PrepareIpv4("127.0.0.2", 33)),
              IsTrue());
  EXPECT_THAT(
      allowed_endpoints.IsEndpointAllowed(PrepareIpv4("120.120.120.255")),
      IsTrue());
  EXPECT_THAT(
      allowed_endpoints.IsEndpointAllowed(PrepareIpv4("120.120.121.120")),
      IsFalse());
  EXPECT_THAT(
      allowed_endpoints.IsEndpointAllowed(PrepareIpv4("130.130.128.130", 1000)),
      IsTrue());
  EXPECT_THAT(
      allowed_endpoints.IsEndpointAllowed(PrepareIpv4("130.130.132.134", 1000)),
      IsFalse());
  EXPECT_THAT(
      allowed_endpoints.IsEndpointAllowed(PrepareIpv4("130.130.128.130", 1001)),
      IsFalse());
  EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(PrepareIpv4("140.0.140.140")),
              IsTrue());
  EXPECT_THAT(
      allowed_endpoints.IsEndpointAllowed(PrepareIpv4("141.140.140.140")),
      IsFalse());
  EXPECT_THAT(
      allowed_endpoints.IsEndpointAllowed(PrepareIpv4("150.182.150.150", 123)),
      IsTrue());
  EXPECT_THAT(
      allowed_endpoints.IsEndpointAllowed(PrepareIpv4("150.214.150.150", 123)),
      IsFalse());

  // IPv6 tests
  EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(PrepareIpv6("::3")),
              IsFalse());
  EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(PrepareIpv6("::2")),
              IsTrue());
  EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(PrepareIpv6("::1")),
              IsTrue());
  EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(PrepareIpv6("::1", 81)),
              IsFalse());
  EXPECT_THAT(
      allowed_endpoints.IsEndpointAllowed(PrepareIpv6("0:1234:ffff:0:0:0:0:0")),
      IsTrue());
  EXPECT_THAT(
      allowed_endpoints.IsEndpointAllowed(PrepareIpv6("0:1233:0000:0:0:0:0:0")),
      IsFalse());
  EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(
                  PrepareIpv6("0:5678:0002:0:0:0:0:0", 70)),
              IsTrue());
  EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(
                  PrepareIpv6("0:5678:0004:0:0:0:0:0", 70)),
              IsFalse());
  EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(
                  PrepareIpv6("0:5678:0000:0:0:0:0:0", 2222)),
              IsFalse());
}

TEST(FilteringTest, UnixSocket) {
  sandbox2::AllowedEndpoints allowed_endpoints;

  EXPECT_THAT(allowed_endpoints.AllowUnixSocket("/tmp/allowed.sock"), IsOk());

  struct sockaddr_un allowed_addr{};
  allowed_addr.sun_family = AF_UNIX;
  strncpy(allowed_addr.sun_path, "/tmp/allowed.sock",
          sizeof(allowed_addr.sun_path) - 1);

  struct sockaddr_un disallowed_addr{};
  disallowed_addr.sun_family = AF_UNIX;
  strncpy(disallowed_addr.sun_path, "/tmp/disallowed.sock",
          sizeof(disallowed_addr.sun_path) - 1);

  struct sockaddr_un abstract_addr{};
  abstract_addr.sun_family = AF_UNIX;
  abstract_addr.sun_path[0] = '\0';
  strncpy(abstract_addr.sun_path + 1, "abstract.sock",
          sizeof(abstract_addr.sun_path) - 2);

  EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(
                  reinterpret_cast<struct sockaddr*>(&allowed_addr),
                  sizeof(allowed_addr)),
              IsTrue());
  EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(
                  reinterpret_cast<struct sockaddr*>(&disallowed_addr),
                  sizeof(disallowed_addr)),
              IsFalse());
  EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(
                  reinterpret_cast<struct sockaddr*>(&abstract_addr),
                  sizeof(abstract_addr)),
              IsFalse());
}

TEST(FilteringTest, UnixSocketDisabledFilterByDefault) {
  sandbox2::AllowedEndpoints allowed_endpoints;
  EXPECT_FALSE(allowed_endpoints.filter_unix_sockets());

  struct sockaddr_un path_addr{};
  path_addr.sun_family = AF_UNIX;
  strncpy(path_addr.sun_path, "/tmp/any.sock", sizeof(path_addr.sun_path) - 1);

  struct sockaddr_un abstract_addr{};
  abstract_addr.sun_family = AF_UNIX;
  abstract_addr.sun_path[0] = '\0';
  strncpy(abstract_addr.sun_path + 1, "abstract.sock",
          sizeof(abstract_addr.sun_path) - 2);

  EXPECT_THAT(
      allowed_endpoints.IsEndpointAllowed(
          reinterpret_cast<struct sockaddr*>(&path_addr), sizeof(path_addr)),
      IsFalse());
  EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(
                  reinterpret_cast<struct sockaddr*>(&abstract_addr),
                  sizeof(abstract_addr)),
              IsFalse());
}

TEST(FilteringTest, UnixSocketEnabledFilterBlocksUnlisted) {
  sandbox2::AllowedEndpoints allowed_endpoints;
  allowed_endpoints.set_filter_unix_sockets(true);
  EXPECT_TRUE(allowed_endpoints.filter_unix_sockets());

  struct sockaddr_un path_addr{};
  path_addr.sun_family = AF_UNIX;
  strncpy(path_addr.sun_path, "/tmp/any.sock", sizeof(path_addr.sun_path) - 1);

  struct sockaddr_un abstract_addr{};
  abstract_addr.sun_family = AF_UNIX;
  abstract_addr.sun_path[0] = '\0';
  strncpy(abstract_addr.sun_path + 1, "abstract.sock",
          sizeof(abstract_addr.sun_path) - 2);
  socklen_t abstract_len =
      offsetof(struct sockaddr_un, sun_path) + 1 + strlen("abstract.sock");

  EXPECT_THAT(
      allowed_endpoints.IsEndpointAllowed(
          reinterpret_cast<struct sockaddr*>(&path_addr), sizeof(path_addr)),
      IsFalse());
  EXPECT_THAT(
      allowed_endpoints.IsEndpointAllowed(
          reinterpret_cast<struct sockaddr*>(&abstract_addr), abstract_len),
      IsFalse());

  std::string abs_name(abstract_addr.sun_path,
                       1 + strlen(abstract_addr.sun_path + 1));
  EXPECT_THAT(allowed_endpoints.AllowUnixSocket(abs_name), IsOk());
  EXPECT_THAT(
      allowed_endpoints.IsEndpointAllowed(
          reinterpret_cast<struct sockaddr*>(&abstract_addr), abstract_len),
      IsTrue());
}

TEST(FilteringTest, AddrToStringUnixBounded) {
  struct sockaddr_un un;
  memset(&un, 'X', sizeof(un));
  un.sun_family = AF_UNIX;
  un.sun_path[0] = '\0';
  memcpy(un.sun_path + 1, "test_abstract", 13);
  // Pass explicit length containing exact bytes without trailing nulls
  socklen_t len = offsetof(struct sockaddr_un, sun_path) + 1 + 13;
  absl::StatusOr<std::string> str =
      sandbox2::AddrToString(reinterpret_cast<struct sockaddr*>(&un), len);
  ASSERT_THAT(str, IsOk());
  EXPECT_EQ(*str, "UNIX Abstract Socket: test_abstract");
}

TEST(FilteringTest, UnixSocketNonNullTerminatedBounded) {
  sandbox2::AllowedEndpoints allowed_endpoints;
  EXPECT_THAT(allowed_endpoints.AllowUnixSocket("/tmp/test.sock"), IsOk());

  struct sockaddr_un un;
  memset(&un, 'Y', sizeof(un));
  un.sun_family = AF_UNIX;
  // Fill sun_path with '/tmp/test.sock' without a trailing null terminator
  memcpy(un.sun_path, "/tmp/test.sock", 14);
  socklen_t len = offsetof(struct sockaddr_un, sun_path) + 14;

  EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(
                  reinterpret_cast<struct sockaddr*>(&un), len),
              IsTrue());
}

TEST(FilteringTest, UnixSocketAbstractExactTrailingNulls) {
  struct sockaddr_un un;
  memset(&un, 0, sizeof(un));
  un.sun_family = AF_UNIX;
  memcpy(un.sun_path, "\0test\0\0", 7);

  socklen_t len_5 = offsetof(struct sockaddr_un, sun_path) + 5;
  socklen_t len_7 = offsetof(struct sockaddr_un, sun_path) + 7;

  // 1. Allowlist 5-byte name: 5-byte matches, 7-byte is blocked.
  {
    sandbox2::AllowedEndpoints allowed_endpoints;
    allowed_endpoints.set_filter_unix_sockets(true);

    std::string short_name("\0test", 5);
    EXPECT_THAT(allowed_endpoints.AllowUnixSocket(short_name), IsOk());

    EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(
                    reinterpret_cast<struct sockaddr*>(&un), len_5),
                IsTrue());
    EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(
                    reinterpret_cast<struct sockaddr*>(&un), len_7),
                IsFalse());
  }

  // 2. Allowlist 7-byte name: 7-byte matches, 5-byte is blocked.
  {
    sandbox2::AllowedEndpoints allowed_endpoints;
    allowed_endpoints.set_filter_unix_sockets(true);

    std::string long_name("\0test\0\0", 7);
    EXPECT_THAT(allowed_endpoints.AllowUnixSocket(long_name), IsOk());

    EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(
                    reinterpret_cast<struct sockaddr*>(&un), len_7),
                IsTrue());
    EXPECT_THAT(allowed_endpoints.IsEndpointAllowed(
                    reinterpret_cast<struct sockaddr*>(&un), len_5),
                IsFalse());
  }
}

TEST(FilteringTest, CanonicalizeSocketPathAbstract) {
  std::string abstract_path("\0my_abstract_socket", 19);
  EXPECT_EQ(sandbox2::AllowedEndpoints::CanonicalizeSocketPath(abstract_path),
            abstract_path);
}

TEST(FilteringTest, CanonicalizeSocketPathSymlinkParent) {
  std::string temp_dir_tpl =
      sapi::file::JoinPath(testing::TempDir(), "sb2_canon_dir_XXXXXX");
  std::vector<char> temp_dir(temp_dir_tpl.begin(), temp_dir_tpl.end());
  temp_dir.push_back('\0');
  ASSERT_NE(mkdtemp(temp_dir.data()), nullptr);

  char resolved_dir[PATH_MAX];
  ASSERT_NE(realpath(temp_dir.data(), resolved_dir), nullptr);

  std::string symlink_path = absl::StrCat(temp_dir.data(), "_symlink");
  unlink(symlink_path.c_str());
  ASSERT_EQ(symlink(resolved_dir, symlink_path.c_str()), 0);

  std::string input_path =
      sapi::file::JoinPath(symlink_path, "sub", "socket.sock");
  std::string expected_path =
      sapi::file::JoinPath(resolved_dir, "sub", "socket.sock");
  EXPECT_EQ(sandbox2::AllowedEndpoints::CanonicalizeSocketPath(input_path),
            expected_path);

  unlink(symlink_path.c_str());
  rmdir(temp_dir.data());
}

}  // namespace
}  // namespace sandbox2
