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

#include "sandboxed_api/sandbox2/network_proxy/client.h"

#include <fcntl.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <netinet/in.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/macros.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/sandbox2/util/syscall_trap.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/thread.h"

namespace sandbox2 {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::sapi::file_util::fileops::FDCloser;
using ::testing::HasSubstr;

sockaddr_in CreateTestIpv4Addr() {
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(80);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  return addr;
}

TEST(ClientTest, ConnectInvalidSocketFdFails) {
  int sv[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  FDCloser server_closer(sv[1]);
  NetworkProxyClient client(sv[0]);

  sockaddr_in addr = CreateTestIpv4Addr();
  EXPECT_THAT(client.Connect(-1, reinterpret_cast<const sockaddr*>(&addr),
                             sizeof(addr)),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("Invalid socket FD")));
}

TEST(ClientTest, ConnectNonStreamSocketFails) {
  int sv[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  FDCloser server_closer(sv[1]);
  NetworkProxyClient client(sv[0]);

  FDCloser dgram_sock(socket(AF_INET, SOCK_DGRAM, 0));
  ASSERT_GE(dgram_sock.get(), 0);

  sockaddr_in addr = CreateTestIpv4Addr();
  errno = 0;
  EXPECT_THAT(
      client.Connect(dgram_sock.get(), reinterpret_cast<const sockaddr*>(&addr),
                     sizeof(addr)),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("only SOCK_STREAM is allowed")));
  EXPECT_EQ(errno, EINVAL);
}

TEST(ClientTest, ConnectSendBytesFails) {
  NetworkProxyClient client(-1);

  FDCloser stream_sock(socket(AF_INET, SOCK_STREAM, 0));
  ASSERT_GE(stream_sock.get(), 0);

  sockaddr_in addr = CreateTestIpv4Addr();
  errno = 0;
  EXPECT_THAT(
      client.Connect(stream_sock.get(),
                     reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Sending data to network proxy failed")));
  EXPECT_EQ(errno, EIO);
}

TEST(ClientTest, ConnectRecvInt32Fails) {
  int sv[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  FDCloser server_closer(sv[1]);
  NetworkProxyClient client(sv[0]);

  // Shut down write on the server side so client's SendBytes succeeds while
  // RecvInt32 hits EOF.
  ASSERT_EQ(shutdown(sv[1], SHUT_WR), 0);

  FDCloser stream_sock(socket(AF_INET, SOCK_STREAM, 0));
  ASSERT_GE(stream_sock.get(), 0);

  sockaddr_in addr = CreateTestIpv4Addr();
  errno = 0;
  EXPECT_THAT(
      client.Connect(stream_sock.get(),
                     reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Receiving data from the network proxy failed")));
  EXPECT_EQ(errno, EIO);
}

TEST(ClientTest, ConnectServerErrorResultFails) {
  int sv[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  NetworkProxyClient client(sv[0]);
  Comms server_comms(sv[1]);

  ASSERT_TRUE(server_comms.SendInt32(ECONNREFUSED));

  FDCloser stream_sock(socket(AF_INET, SOCK_STREAM, 0));
  ASSERT_GE(stream_sock.get(), 0);

  sockaddr_in addr = CreateTestIpv4Addr();
  errno = 0;
  EXPECT_THAT(
      client.Connect(stream_sock.get(),
                     reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)),
      StatusIs(absl::StatusCode::kUnavailable,
               HasSubstr("Error in network proxy server")));
  EXPECT_EQ(errno, ECONNREFUSED);
}

TEST(ClientTest, ConnectRecvFdFails) {
  int sv[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  NetworkProxyClient client(sv[0]);
  Comms server_comms(sv[1]);

  ASSERT_TRUE(server_comms.SendInt32(0));
  ASSERT_EQ(shutdown(server_comms.GetConnectionFD(), SHUT_WR), 0);

  FDCloser stream_sock(socket(AF_INET, SOCK_STREAM, 0));
  ASSERT_GE(stream_sock.get(), 0);

  sockaddr_in addr = CreateTestIpv4Addr();
  errno = 0;
  EXPECT_THAT(
      client.Connect(stream_sock.get(),
                     reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Receiving fd from network proxy failed")));
  EXPECT_EQ(errno, EIO);
}

TEST(ClientTest, ConnectRestoreSocketFlagsFails) {
  int sv[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  NetworkProxyClient client(sv[0]);
  Comms server_comms(sv[1]);

  // An O_PATH descriptor can be transferred over SCM_RIGHTS, but fcntl(F_SETFL)
  // on it fails with EBADF.
  FDCloser o_path_fd(open("/dev/null", O_PATH | O_CLOEXEC));
  ASSERT_GE(o_path_fd.get(), 0);
  ASSERT_TRUE(server_comms.SendInt32(0));
  ASSERT_TRUE(server_comms.SendFD(o_path_fd.get()));

  FDCloser stream_sock(socket(AF_INET, SOCK_STREAM, 0));
  ASSERT_GE(stream_sock.get(), 0);

  sockaddr_in addr = CreateTestIpv4Addr();
  EXPECT_THAT(
      client.Connect(stream_sock.get(),
                     reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Failed to restore socket flags")));
}

TEST(ClientTest, ConnectDup2Fails) {
  int sv[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  NetworkProxyClient client(sv[0]);
  Comms server_comms(sv[1]);

  FDCloser proxy_sock(socket(AF_INET, SOCK_STREAM, 0));
  ASSERT_GE(proxy_sock.get(), 0);
  ASSERT_TRUE(server_comms.SendInt32(0));
  ASSERT_TRUE(server_comms.SendFD(proxy_sock.get()));

  FDCloser base_sock(socket(AF_INET, SOCK_STREAM, 0));
  ASSERT_GE(base_sock.get(), 0);
  FDCloser high_sock(fcntl(base_sock.get(), F_DUPFD_CLOEXEC, 256));
  ASSERT_GE(high_sock.get(), 256);

  rlimit old_rlim = {};
  ASSERT_EQ(getrlimit(RLIMIT_NOFILE, &old_rlim), 0);
  rlimit low_rlim = old_rlim;
  low_rlim.rlim_cur = high_sock.get();
  ASSERT_EQ(setrlimit(RLIMIT_NOFILE, &low_rlim), 0);
  absl::Cleanup restore_rlim = [&old_rlim] {
    EXPECT_EQ(setrlimit(RLIMIT_NOFILE, &old_rlim), 0);
  };

  sockaddr_in addr = CreateTestIpv4Addr();
  EXPECT_THAT(
      client.Connect(high_sock.get(), reinterpret_cast<const sockaddr*>(&addr),
                     sizeof(addr)),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Duplicating socket failed")));
}

TEST(ClientTest, InstallNetworkProxyHandlerAlreadyInstalledFails) {
  int sv[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  FDCloser server_closer(sv[1]);
  NetworkProxyClient client(sv[0]);

  absl::Cleanup cleanup = [] {
    SyscallTrap::Uninstall();
    NetworkProxyHandler::network_proxy_client_ = nullptr;
  };

  ASSERT_THAT(NetworkProxyHandler::InstallNetworkProxyHandler(&client), IsOk());
  EXPECT_THAT(
      NetworkProxyHandler::InstallNetworkProxyHandler(&client),
      StatusIs(absl::StatusCode::kAlreadyExists,
               HasSubstr("Network proxy handler is already installed")));
}

TEST(ClientTest, InstallNetworkProxyHandlerSyscallTrapInstallFails) {
  int sv[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  FDCloser server_closer(sv[1]);
  NetworkProxyClient client(sv[0]);

  ASSERT_TRUE(SyscallTrap::Install(
      [](int, SyscallTrap::Args, uintptr_t*) { return false; }));
  absl::Cleanup cleanup = [] {
    SyscallTrap::Uninstall();
    NetworkProxyHandler::network_proxy_client_ = nullptr;
  };

  EXPECT_THAT(NetworkProxyHandler::InstallNetworkProxyHandler(&client),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Could not install syscall trap")));
}

TEST(ClientTest, ProcessSeccompTrapUnhandledSyscallReturnsFalse) {
  uintptr_t rv = 123;
  EXPECT_FALSE(NetworkProxyHandler::ProcessSeccompTrap(__NR_read, {}, &rv));
  EXPECT_EQ(rv, 123);
}

TEST(ClientTest, ProcessSeccompTrapConnectFailureAndSuccess) {
  int sv[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  NetworkProxyClient client(sv[0]);
  Comms server_comms(sv[1]);

  absl::Cleanup cleanup = [] {
    SyscallTrap::Uninstall();
    NetworkProxyHandler::network_proxy_client_ = nullptr;
  };
  ASSERT_THAT(NetworkProxyHandler::InstallNetworkProxyHandler(&client), IsOk());

  // 1. Failure via ProcessSeccompTrap sets *rv = -errno.
  sockaddr_in addr = CreateTestIpv4Addr();
  uintptr_t rv = 0;
  SyscallTrap::Args fail_args = {
      static_cast<uintptr_t>(-1),
      reinterpret_cast<uintptr_t>(&addr),
      sizeof(addr),
      0,
      0,
      0,
  };
  EXPECT_TRUE(
      NetworkProxyHandler::ProcessSeccompTrap(__NR_connect, fail_args, &rv));
  EXPECT_EQ(rv, static_cast<uintptr_t>(-EBADF));

  // 2. Success via trapped connect() syscall invokes installed trap handler and
  // sets *rv = 0.
  FDCloser proxy_sock(socket(AF_INET, SOCK_STREAM, 0));
  ASSERT_GE(proxy_sock.get(), 0);
  ASSERT_TRUE(server_comms.SendInt32(0));
  ASSERT_TRUE(server_comms.SendFD(proxy_sock.get()));

  FDCloser client_sock(socket(AF_INET, SOCK_STREAM, 0));
  ASSERT_GE(client_sock.get(), 0);

  sapi::Thread thread([&client_sock, &addr] {
    sock_filter filter[] = {
        LOAD_SYSCALL_NR,
        SYSCALL(__NR_connect, TRAP(0)),
        ALLOW,
    };
    sock_fprog prog = {
        .len = ABSL_ARRAYSIZE(filter),
        .filter = filter,
    };
    CHECK_EQ(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0), 0);
    CHECK_EQ(prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog), 0);
    EXPECT_EQ(connect(client_sock.get(), reinterpret_cast<sockaddr*>(&addr),
                      sizeof(addr)),
              0);
  });
  thread.Join();
}

}  // namespace
}  // namespace sandbox2
