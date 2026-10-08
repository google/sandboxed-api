// Copyright 2026 Google LLC
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

#include "sandboxed_api/sandbox2/network_proxy/server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/network_proxy/filtering.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/temp_file.h"

namespace sandbox2 {
namespace {

using ::absl_testing::IsOk;
using ::sapi::file_util::fileops::FDCloser;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::IsFalse;
using ::testing::IsTrue;

std::string CreateTempPath() {
  absl::StatusOr<std::string> path =
      sapi::CreateNamedTempFileAndClose(sapi::GetTestTempPath("srv_test_"));
  CHECK_OK(path);
  unlink(path->c_str());
  return *path;
}

sockaddr_in MakeIpv4Addr(const char* ip, uint16_t port = 80) {
  sockaddr_in saddr{};
  saddr.sin_family = AF_INET;
  saddr.sin_port = htons(port);
  CHECK_EQ(inet_pton(AF_INET, ip, &saddr.sin_addr), 1);
  return saddr;
}

TEST(NetworkProxyServerTest, RecvBytesFailureShutsDownServer) {
  int sv[2];
  ASSERT_THAT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), Eq(0));
  Comms client_comms(sv[1]);
  AllowedEndpoints allowed_endpoints;
  bool notified = false;
  NetworkProxyServer server(sv[0], &allowed_endpoints,
                            [&] { notified = true; });

  ASSERT_THAT(client_comms.SendInt32(42), IsTrue());
  server.Run();

  EXPECT_THAT(server.violation_occurred_.load(), IsFalse());
  EXPECT_THAT(notified, IsFalse());
}

TEST(NetworkProxyServerTest, UnsupportedAddressFamilyReturnsEinval) {
  int sv[2];
  ASSERT_THAT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), Eq(0));
  Comms client_comms(sv[1]);
  AllowedEndpoints allowed_endpoints;
  bool notified = false;
  NetworkProxyServer server(sv[0], &allowed_endpoints,
                            [&] { notified = true; });
  sockaddr saddr{};
  saddr.sa_family = AF_UNSPEC;

  ASSERT_THAT(client_comms.SendBytes(reinterpret_cast<const uint8_t*>(&saddr),
                                     sizeof(saddr)),
              IsTrue());
  ASSERT_THAT(shutdown(sv[1], SHUT_WR), Eq(0));
  server.Run();

  int32_t err = 0;
  ASSERT_THAT(client_comms.RecvInt32(&err), IsTrue());
  EXPECT_THAT(err, Eq(EINVAL));
  EXPECT_THAT(server.violation_occurred_.load(), IsFalse());
  EXPECT_THAT(notified, IsFalse());
}

TEST(NetworkProxyServerTest, InvalidIpv4AddressSizeReturnsEinval) {
  int sv[2];
  ASSERT_THAT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), Eq(0));
  Comms client_comms(sv[1]);
  AllowedEndpoints allowed_endpoints;
  bool notified = false;
  NetworkProxyServer server(sv[0], &allowed_endpoints,
                            [&] { notified = true; });
  sockaddr_in saddr = MakeIpv4Addr("127.0.0.1");

  ASSERT_THAT(client_comms.SendBytes(reinterpret_cast<const uint8_t*>(&saddr),
                                     sizeof(saddr) - 1),
              IsTrue());
  ASSERT_THAT(shutdown(sv[1], SHUT_WR), Eq(0));
  server.Run();

  int32_t err = 0;
  ASSERT_THAT(client_comms.RecvInt32(&err), IsTrue());
  EXPECT_THAT(err, Eq(EINVAL));
  EXPECT_THAT(server.violation_occurred_.load(), IsFalse());
  EXPECT_THAT(notified, IsFalse());
}

TEST(NetworkProxyServerTest, InvalidIpv6AddressSizeReturnsEinval) {
  int sv[2];
  ASSERT_THAT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), Eq(0));
  Comms client_comms(sv[1]);
  AllowedEndpoints allowed_endpoints;
  bool notified = false;
  NetworkProxyServer server(sv[0], &allowed_endpoints,
                            [&] { notified = true; });
  sockaddr_in6 saddr{};
  saddr.sin6_family = AF_INET6;

  ASSERT_THAT(client_comms.SendBytes(reinterpret_cast<const uint8_t*>(&saddr),
                                     sizeof(saddr) - 1),
              IsTrue());
  ASSERT_THAT(shutdown(sv[1], SHUT_WR), Eq(0));
  server.Run();

  int32_t err = 0;
  ASSERT_THAT(client_comms.RecvInt32(&err), IsTrue());
  EXPECT_THAT(err, Eq(EINVAL));
  EXPECT_THAT(server.violation_occurred_.load(), IsFalse());
  EXPECT_THAT(notified, IsFalse());
}

TEST(NetworkProxyServerTest, EmptyUnixSocketAddressReturnsEinval) {
  int sv[2];
  ASSERT_THAT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), Eq(0));
  Comms client_comms(sv[1]);
  AllowedEndpoints allowed_endpoints;
  bool notified = false;
  NetworkProxyServer server(sv[0], &allowed_endpoints,
                            [&] { notified = true; });
  sockaddr_un saddr{};
  saddr.sun_family = AF_UNIX;

  ASSERT_THAT(client_comms.SendBytes(reinterpret_cast<const uint8_t*>(&saddr),
                                     offsetof(struct sockaddr_un, sun_path)),
              IsTrue());
  ASSERT_THAT(shutdown(sv[1], SHUT_WR), Eq(0));
  server.Run();

  int32_t err = 0;
  ASSERT_THAT(client_comms.RecvInt32(&err), IsTrue());
  EXPECT_THAT(err, Eq(EINVAL));
  EXPECT_THAT(server.violation_occurred_.load(), IsFalse());
  EXPECT_THAT(notified, IsFalse());
}

TEST(NetworkProxyServerTest, DisallowedEndpointTriggersViolation) {
  int sv[2];
  ASSERT_THAT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), Eq(0));
  Comms client_comms(sv[1]);
  AllowedEndpoints allowed_endpoints;
  bool notified = false;
  NetworkProxyServer server(sv[0], &allowed_endpoints,
                            [&] { notified = true; });
  sockaddr_in saddr = MakeIpv4Addr("127.0.0.1", 8080);

  ASSERT_THAT(client_comms.SendBytes(reinterpret_cast<const uint8_t*>(&saddr),
                                     sizeof(saddr)),
              IsTrue());
  server.Run();

  EXPECT_THAT(server.violation_occurred_.load(), IsTrue());
  EXPECT_THAT(notified, IsTrue());
  EXPECT_THAT(server.violation_msg_, Eq("IP: 127.0.0.1, port: 8080"));
}

TEST(NetworkProxyServerTest, UnixSocketNonExistentPathReturnsEnoent) {
  std::string sock_path = CreateTempPath();
  AllowedEndpoints allowed_endpoints;
  ASSERT_THAT(allowed_endpoints.AllowUnixSocket(sock_path), IsOk());

  int sv[2];
  ASSERT_THAT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), Eq(0));
  Comms client_comms(sv[1]);
  bool notified = false;
  NetworkProxyServer server(sv[0], &allowed_endpoints,
                            [&] { notified = true; });

  sockaddr_un saddr{};
  saddr.sun_family = AF_UNIX;
  strncpy(saddr.sun_path, sock_path.c_str(), sizeof(saddr.sun_path) - 1);
  socklen_t saddr_len =
      offsetof(struct sockaddr_un, sun_path) + sock_path.size() + 1;

  ASSERT_THAT(client_comms.SendBytes(reinterpret_cast<const uint8_t*>(&saddr),
                                     saddr_len),
              IsTrue());
  ASSERT_THAT(shutdown(sv[1], SHUT_WR), Eq(0));
  server.Run();

  int32_t err = 0;
  ASSERT_THAT(client_comms.RecvInt32(&err), IsTrue());
  EXPECT_THAT(err, Eq(ENOENT));
  EXPECT_THAT(server.violation_occurred_.load(), IsFalse());
  EXPECT_THAT(notified, IsFalse());
}

TEST(NetworkProxyServerTest, UnixSocketSymlinkReturnsEloop) {
  std::string symlink_path = CreateTempPath();
  std::string target_path = absl::StrCat(symlink_path, "_target");
  ASSERT_THAT(symlink(target_path.c_str(), symlink_path.c_str()), Eq(0));
  absl::Cleanup cleanup = [&] { unlink(symlink_path.c_str()); };

  AllowedEndpoints allowed_endpoints;
  ASSERT_THAT(allowed_endpoints.AllowUnixSocket(symlink_path), IsOk());

  int sv[2];
  ASSERT_THAT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), Eq(0));
  Comms client_comms(sv[1]);
  bool notified = false;
  NetworkProxyServer server(sv[0], &allowed_endpoints,
                            [&] { notified = true; });

  sockaddr_un saddr{};
  saddr.sun_family = AF_UNIX;
  strncpy(saddr.sun_path, symlink_path.c_str(), sizeof(saddr.sun_path) - 1);
  socklen_t saddr_len =
      offsetof(struct sockaddr_un, sun_path) + symlink_path.size() + 1;

  ASSERT_THAT(client_comms.SendBytes(reinterpret_cast<const uint8_t*>(&saddr),
                                     saddr_len),
              IsTrue());
  ASSERT_THAT(shutdown(sv[1], SHUT_WR), Eq(0));
  server.Run();

  int32_t err = 0;
  ASSERT_THAT(client_comms.RecvInt32(&err), IsTrue());
  EXPECT_THAT(err, Eq(ELOOP));
  EXPECT_THAT(server.violation_occurred_.load(), IsFalse());
  EXPECT_THAT(notified, IsFalse());
}

TEST(NetworkProxyServerTest, UnixSocketRegularFileReturnsEnotsock) {
  SAPI_ASSERT_OK_AND_ASSIGN(
      std::string file_path,
      sapi::CreateNamedTempFileAndClose(sapi::GetTestTempPath("srv_test_")));
  absl::Cleanup cleanup = [&] { unlink(file_path.c_str()); };

  AllowedEndpoints allowed_endpoints;
  ASSERT_THAT(allowed_endpoints.AllowUnixSocket(file_path), IsOk());

  int sv[2];
  ASSERT_THAT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), Eq(0));
  Comms client_comms(sv[1]);
  bool notified = false;
  NetworkProxyServer server(sv[0], &allowed_endpoints,
                            [&] { notified = true; });

  sockaddr_un saddr{};
  saddr.sun_family = AF_UNIX;
  strncpy(saddr.sun_path, file_path.c_str(), sizeof(saddr.sun_path) - 1);
  socklen_t saddr_len =
      offsetof(struct sockaddr_un, sun_path) + file_path.size() + 1;

  ASSERT_THAT(client_comms.SendBytes(reinterpret_cast<const uint8_t*>(&saddr),
                                     saddr_len),
              IsTrue());
  ASSERT_THAT(shutdown(sv[1], SHUT_WR), Eq(0));
  server.Run();

  int32_t err = 0;
  ASSERT_THAT(client_comms.RecvInt32(&err), IsTrue());
  EXPECT_THAT(err, Eq(ENOTSOCK));
  EXPECT_THAT(server.violation_occurred_.load(), IsFalse());
  EXPECT_THAT(notified, IsFalse());
}

TEST(NetworkProxyServerTest, SocketCreationFailureReturnsEmfile) {
  AllowedEndpoints allowed_endpoints;
  ASSERT_THAT(allowed_endpoints.AllowIPv4("127.0.0.1"), IsOk());

  int sv[2];
  ASSERT_THAT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), Eq(0));
  Comms client_comms(sv[1]);
  bool notified = false;
  NetworkProxyServer server(sv[0], &allowed_endpoints,
                            [&] { notified = true; });
  sockaddr_in saddr = MakeIpv4Addr("127.0.0.1");

  ASSERT_THAT(client_comms.SendBytes(reinterpret_cast<const uint8_t*>(&saddr),
                                     sizeof(saddr)),
              IsTrue());
  ASSERT_THAT(shutdown(sv[1], SHUT_WR), Eq(0));

  rlimit old_rlim{};
  ASSERT_THAT(getrlimit(RLIMIT_NOFILE, &old_rlim), Eq(0));
  rlimit zero_rlim = {0, old_rlim.rlim_max};
  ASSERT_THAT(setrlimit(RLIMIT_NOFILE, &zero_rlim), Eq(0));
  {
    absl::Cleanup restore_rlim = [&] { setrlimit(RLIMIT_NOFILE, &old_rlim); };
    server.Run();
  }

  int32_t err = 0;
  ASSERT_THAT(client_comms.RecvInt32(&err), IsTrue());
  EXPECT_THAT(err, Eq(EMFILE));
  EXPECT_THAT(server.violation_occurred_.load(), IsFalse());
  EXPECT_THAT(notified, IsFalse());
}

TEST(NetworkProxyServerTest, UnixPathSocketConnectFailureReturnsEconnrefused) {
  std::string sock_path = CreateTempPath();
  FDCloser host_sock(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
  ASSERT_THAT(host_sock.get(), Ge(0));

  sockaddr_un saddr{};
  saddr.sun_family = AF_UNIX;
  strncpy(saddr.sun_path, sock_path.c_str(), sizeof(saddr.sun_path) - 1);
  socklen_t saddr_len =
      offsetof(struct sockaddr_un, sun_path) + sock_path.size() + 1;
  ASSERT_THAT(
      bind(host_sock.get(), reinterpret_cast<sockaddr*>(&saddr), saddr_len),
      Eq(0));
  absl::Cleanup cleanup = [&] { unlink(sock_path.c_str()); };

  AllowedEndpoints allowed_endpoints;
  ASSERT_THAT(allowed_endpoints.AllowUnixSocket(sock_path), IsOk());

  int sv[2];
  ASSERT_THAT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), Eq(0));
  Comms client_comms(sv[1]);
  bool notified = false;
  NetworkProxyServer server(sv[0], &allowed_endpoints,
                            [&] { notified = true; });

  ASSERT_THAT(client_comms.SendBytes(reinterpret_cast<const uint8_t*>(&saddr),
                                     saddr_len),
              IsTrue());
  ASSERT_THAT(shutdown(sv[1], SHUT_WR), Eq(0));
  server.Run();

  int32_t err = 0;
  ASSERT_THAT(client_comms.RecvInt32(&err), IsTrue());
  EXPECT_THAT(err, Eq(ECONNREFUSED));
  EXPECT_THAT(server.violation_occurred_.load(), IsFalse());
  EXPECT_THAT(notified, IsFalse());
}

TEST(NetworkProxyServerTest,
     AbstractUnixSocketConnectFailureReturnsEconnrefused) {
  std::string abstract_name("\0non_existent_abstract_socket", 29);
  AllowedEndpoints allowed_endpoints;
  ASSERT_THAT(allowed_endpoints.AllowUnixSocket(abstract_name), IsOk());

  int sv[2];
  ASSERT_THAT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), Eq(0));
  Comms client_comms(sv[1]);
  bool notified = false;
  NetworkProxyServer server(sv[0], &allowed_endpoints,
                            [&] { notified = true; });

  sockaddr_un saddr{};
  saddr.sun_family = AF_UNIX;
  memcpy(saddr.sun_path, abstract_name.data(), abstract_name.size());
  socklen_t saddr_len =
      offsetof(struct sockaddr_un, sun_path) + abstract_name.size();

  ASSERT_THAT(client_comms.SendBytes(reinterpret_cast<const uint8_t*>(&saddr),
                                     saddr_len),
              IsTrue());
  ASSERT_THAT(shutdown(sv[1], SHUT_WR), Eq(0));
  server.Run();

  int32_t err = 0;
  ASSERT_THAT(client_comms.RecvInt32(&err), IsTrue());
  EXPECT_THAT(err, Eq(ECONNREFUSED));
  EXPECT_THAT(server.violation_occurred_.load(), IsFalse());
  EXPECT_THAT(notified, IsFalse());
}

TEST(NetworkProxyServerTest, SendErrorFailureShutsDownServer) {
  int sv[2];
  ASSERT_THAT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), Eq(0));
  Comms client_comms(sv[1]);
  AllowedEndpoints allowed_endpoints;
  bool notified = false;
  NetworkProxyServer server(sv[0], &allowed_endpoints,
                            [&] { notified = true; });
  sockaddr saddr{};
  saddr.sa_family = AF_UNSPEC;

  ASSERT_THAT(client_comms.SendBytes(reinterpret_cast<const uint8_t*>(&saddr),
                                     sizeof(saddr)),
              IsTrue());
  client_comms.Terminate();
  server.Run();

  EXPECT_THAT(server.violation_occurred_.load(), IsFalse());
  EXPECT_THAT(notified, IsFalse());
}

TEST(NetworkProxyServerTest, NotifySuccessFailureShutsDownServer) {
  std::string sock_path = CreateTempPath();
  FDCloser host_sock(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
  ASSERT_THAT(host_sock.get(), Ge(0));

  sockaddr_un saddr{};
  saddr.sun_family = AF_UNIX;
  strncpy(saddr.sun_path, sock_path.c_str(), sizeof(saddr.sun_path) - 1);
  socklen_t saddr_len =
      offsetof(struct sockaddr_un, sun_path) + sock_path.size() + 1;
  ASSERT_THAT(
      bind(host_sock.get(), reinterpret_cast<sockaddr*>(&saddr), saddr_len),
      Eq(0));
  ASSERT_THAT(listen(host_sock.get(), 1), Eq(0));
  absl::Cleanup cleanup = [&] { unlink(sock_path.c_str()); };

  AllowedEndpoints allowed_endpoints;
  ASSERT_THAT(allowed_endpoints.AllowUnixSocket(sock_path), IsOk());

  int sv[2];
  ASSERT_THAT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), Eq(0));
  Comms client_comms(sv[1]);
  bool notified = false;
  NetworkProxyServer server(sv[0], &allowed_endpoints,
                            [&] { notified = true; });

  ASSERT_THAT(client_comms.SendBytes(reinterpret_cast<const uint8_t*>(&saddr),
                                     saddr_len),
              IsTrue());
  client_comms.Terminate();
  server.Run();

  EXPECT_THAT(server.violation_occurred_.load(), IsFalse());
  EXPECT_THAT(notified, IsFalse());
}

TEST(NetworkProxyServerTest, SendFdFailureAfterNotifySuccessShutsDownServer) {
  std::string sock_path = CreateTempPath();
  FDCloser host_sock(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
  ASSERT_THAT(host_sock.get(), Ge(0));

  sockaddr_un saddr{};
  saddr.sun_family = AF_UNIX;
  strncpy(saddr.sun_path, sock_path.c_str(), sizeof(saddr.sun_path) - 1);
  socklen_t saddr_len =
      offsetof(struct sockaddr_un, sun_path) + sock_path.size() + 1;
  ASSERT_THAT(
      bind(host_sock.get(), reinterpret_cast<sockaddr*>(&saddr), saddr_len),
      Eq(0));
  ASSERT_THAT(listen(host_sock.get(), 1), Eq(0));
  absl::Cleanup cleanup = [&] { unlink(sock_path.c_str()); };

  AllowedEndpoints allowed_endpoints;
  ASSERT_THAT(allowed_endpoints.AllowUnixSocket(sock_path), IsOk());

  int mem_fd = memfd_create("sb2_srv_test_comms", MFD_CLOEXEC);
  ASSERT_THAT(mem_fd, Ge(0));
  {
    int dup_fd = dup(mem_fd);
    ASSERT_THAT(dup_fd, Ge(0));
    Comms writer(dup_fd);
    ASSERT_THAT(
        writer.SendBytes(reinterpret_cast<const uint8_t*>(&saddr), saddr_len),
        IsTrue());
  }
  ASSERT_THAT(lseek(mem_fd, 0, SEEK_SET), Eq(0));

  bool notified = false;
  NetworkProxyServer server(mem_fd, &allowed_endpoints,
                            [&] { notified = true; });
  server.Run();

  EXPECT_THAT(server.violation_occurred_.load(), IsFalse());
  EXPECT_THAT(notified, IsFalse());
}

}  // namespace
}  // namespace sandbox2
