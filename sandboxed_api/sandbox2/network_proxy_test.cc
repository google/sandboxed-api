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

#include <sys/socket.h>
#include <sys/un.h>
#include <syscall.h>
#include <unistd.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "sandboxed_api/sandbox2/allowlists/enable_landlock.h"
#include "sandboxed_api/sandbox2/allowlists/map_exec.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/landlock.h"
#include "sandboxed_api/sandbox2/network_proxy/testing.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/fileops.h"

namespace sandbox2 {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::sapi::GetTestSourcePath;
using ::testing::Eq;

TEST(NetworkProxyTest, NoDoublePolicy) {
  PolicyBuilder builder;
  builder.AddNetworkProxyHandlerPolicy().AddNetworkProxyPolicy();
  EXPECT_THAT(builder.TryBuild(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(NetworkProxyTest, NoDoublePolicyHandler) {
  PolicyBuilder builder;
  builder.AddNetworkProxyPolicy().AddNetworkProxyHandlerPolicy();
  EXPECT_THAT(builder.TryBuild(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(NetworkProxyTest, NoNetworkPolicyIpv4) {
  PolicyBuilder builder;
  builder.AllowIPv4("127.0.0.1");
  EXPECT_THAT(builder.TryBuild(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(NetworkProxyTest, NoNetworkPolicyIpv6) {
  PolicyBuilder builder;
  builder.AllowIPv6("::1");
  EXPECT_THAT(builder.TryBuild(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(NetworkProxyTest, NoNetworkPolicyUnixSocket) {
  PolicyBuilder builder;
  builder.AllowUnixSocket("/tmp/test.sock");
  EXPECT_THAT(builder.TryBuild(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(NetworkProxyTest, WrongIPv4) {
  PolicyBuilder builder;
  builder.AddNetworkProxyPolicy().AllowIPv4("256.256.256.256");
  EXPECT_THAT(builder.TryBuild(), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(NetworkProxyTest, WrongIPv6) {
  PolicyBuilder builder;
  builder.AddNetworkProxyPolicy().AllowIPv6("127.0.0.1");
  EXPECT_THAT(builder.TryBuild(), StatusIs(absl::StatusCode::kInvalidArgument));
}

using NetworkProxyTest = ::testing::TestWithParam<std::tuple<bool, bool>>;

TEST_P(NetworkProxyTest, ProxyWithHandlerAllowed) {
  SKIP_SANITIZERS;
  const auto [ipv6, use_unotify] = GetParam();
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/network_proxy");
  std::vector<std::string> args = {"network_proxy"};
  if (ipv6) {
    args.push_back("--ipv6");
  }
  auto executor = std::make_unique<Executor>(path, args);
  executor->limits()->set_walltime_limit(absl::Seconds(3));

  PolicyBuilder builder;
  builder.AllowDynamicStartup(sandbox2::MapExec())
      .AllowWrite()
      .AllowRead()
      .AllowExit()
      .AllowSyscall(__NR_sendto)
      .AllowTcMalloc()
      .AddNetworkProxyHandlerPolicy()
      .AllowLlvmCoverage()
      .AddLibrariesForBinary(path);

  if (use_unotify) {
    builder.CollectStacktracesOnSignal(false);
  }
  if (ipv6) {
    builder.AllowIPv6("::1");
  } else {
    builder.AllowIPv4("127.0.0.1");
  }

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, builder.TryBuild());

  Sandbox2 s2(std::move(executor), std::move(policy));
  if (use_unotify) {
    ASSERT_THAT(s2.EnableUnotifyMonitor(), IsOk());
  }
  ASSERT_TRUE(s2.RunAsync());

  SAPI_ASSERT_OK_AND_ASSIGN(auto server, NetworkProxyTestServer::Start(ipv6));
  ASSERT_TRUE(s2.comms()->SendInt32(server->port()));

  sandbox2::Result result = s2.AwaitResult();
  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(EXIT_SUCCESS));
}

TEST_P(NetworkProxyTest, ProxyWithHandlerNotAllowed) {
  SKIP_SANITIZERS;
  const auto [ipv6, use_unotify] = GetParam();
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/network_proxy");
  std::vector<std::string> args = {"network_proxy"};
  if (ipv6) {
    args.push_back("--ipv6");
  }
  auto executor = std::make_unique<Executor>(path, args);
  executor->limits()->set_walltime_limit(absl::Seconds(3));

  PolicyBuilder builder;
  builder.AllowDynamicStartup(sandbox2::MapExec())
      .AllowWrite()
      .AllowRead()
      .AllowExit()
      .AllowSyscall(__NR_sendto)
      .AllowTcMalloc()
      .AddNetworkProxyHandlerPolicy()
      .AllowLlvmCoverage()
      .AddLibrariesForBinary(path);
  if (use_unotify) {
    builder.CollectStacktracesOnSignal(false);
  }
  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, builder.TryBuild());

  Sandbox2 s2(std::move(executor), std::move(policy));
  if (use_unotify) {
    ASSERT_THAT(s2.EnableUnotifyMonitor(), IsOk());
  }
  ASSERT_TRUE(s2.RunAsync());

  SAPI_ASSERT_OK_AND_ASSIGN(auto server, NetworkProxyTestServer::Start(ipv6));
  ASSERT_TRUE(s2.comms()->SendInt32(server->port()));

  sandbox2::Result result = s2.AwaitResult();
  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(Result::VIOLATION_NETWORK));
}

TEST_P(NetworkProxyTest, ProxyWithoutHandlerAllowed) {
  SKIP_SANITIZERS;
  const auto [ipv6, use_unotify] = GetParam();
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/network_proxy");
  std::vector<std::string> args = {"network_proxy", "--noconnect_with_handler"};
  if (ipv6) {
    args.push_back("--ipv6");
  }
  auto executor = std::make_unique<Executor>(path, args);
  executor->limits()->set_walltime_limit(absl::Seconds(3));

  PolicyBuilder builder;
  builder.AllowDynamicStartup(sandbox2::MapExec())
      .AllowExit()
      .AllowWrite()
      .AllowRead()
      .AllowSyscall(__NR_sendto)
      .AllowTcMalloc()
      .AddNetworkProxyHandlerPolicy()
      .AllowLlvmCoverage()
      .AddLibrariesForBinary(path);
  if (use_unotify) {
    builder.CollectStacktracesOnSignal(false);
  }
  if (ipv6) {
    builder.AllowIPv6("::1");
  } else {
    builder.AllowIPv4("127.0.0.1");
  }

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, builder.TryBuild());

  Sandbox2 s2(std::move(executor), std::move(policy));
  if (use_unotify) {
    ASSERT_THAT(s2.EnableUnotifyMonitor(), IsOk());
  }
  ASSERT_TRUE(s2.RunAsync());

  SAPI_ASSERT_OK_AND_ASSIGN(auto server, NetworkProxyTestServer::Start(ipv6));
  ASSERT_TRUE(s2.comms()->SendInt32(server->port()));

  sandbox2::Result result = s2.AwaitResult();
  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(EXIT_SUCCESS));
}

TEST(NetworkProxyTest, ProxyNonExistantAddress) {
  // Creates a IPv6 server tries to connect with IPv4
  SKIP_SANITIZERS;
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/network_proxy");
  std::vector<std::string> args = {"network_proxy", "--noconnect_with_handler"};
  auto executor = std::make_unique<Executor>(path, args);
  executor->limits()->set_walltime_limit(absl::Seconds(3));

  PolicyBuilder builder;
  builder.AllowDynamicStartup(sandbox2::MapExec())
      .AllowExit()
      .AllowWrite()
      .AllowRead()
      .AllowSyscall(__NR_sendto)
      .AllowTcMalloc()
      .AddNetworkProxyHandlerPolicy()
      .AllowLlvmCoverage()
      .AddLibrariesForBinary(path)
      .AllowIPv4("127.0.0.1");

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, builder.TryBuild());

  Sandbox2 s2(std::move(executor), std::move(policy));
  ASSERT_TRUE(s2.RunAsync());

  SAPI_ASSERT_OK_AND_ASSIGN(auto server,
                            NetworkProxyTestServer::Start(/*ipv6=*/true));
  ASSERT_TRUE(s2.comms()->SendInt32(server->port()));

  sandbox2::Result result = s2.AwaitResult();
  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(3));
}

static std::string CreateTempUnixSocketPath() {
  std::string pattern = "/tmp/sb2_unix_test_XXXXXX";
  std::vector<char> template_path(pattern.begin(), pattern.end());
  template_path.push_back('\0');
  int fd = mkstemp(template_path.data());
  CHECK_GE(fd, 0);
  close(fd);
  unlink(template_path.data());
  return std::string(template_path.data());
}

TEST(NetworkProxyTest, ProxyConnectUnixSocketAllowed) {
  SKIP_SANITIZERS;
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/network_proxy");
  const std::string sock_path = CreateTempUnixSocketPath();

  int host_sock = socket(AF_UNIX, SOCK_STREAM, 0);
  ASSERT_GE(host_sock, 0);
  sapi::file_util::fileops::FDCloser host_closer(host_sock);

  struct timeval tv;
  tv.tv_sec = 2;
  tv.tv_usec = 0;
  ASSERT_EQ(setsockopt(host_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)), 0);

  struct sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);
  socklen_t addr_len =
      offsetof(struct sockaddr_un, sun_path) + strlen(addr.sun_path) + 1;
  ASSERT_EQ(
      bind(host_sock, reinterpret_cast<struct sockaddr*>(&addr), addr_len), 0);
  ASSERT_EQ(listen(host_sock, 1), 0);

  std::vector<std::string> args = {
      "network_proxy",
      "--test_connect_unix",
      absl::StrCat("--unix_socket_path=", sock_path),
  };
  auto executor = std::make_unique<Executor>(path, args);
  executor->limits()->set_walltime_limit(absl::Seconds(3));

  PolicyBuilder builder;
  builder.AllowDynamicStartup(sandbox2::MapExec())
      .AllowExit()
      .AllowWrite()
      .AllowRead()
      .AllowTcMalloc()
      .AddNetworkProxyHandlerPolicy(/*filter_unix_sockets=*/true)
      .AllowLlvmCoverage()
      .AddLibrariesForBinary(path)
      .AddDirectory("/sys", false)
      .AddDirectory("/proc", false)
      .AllowUnixSocket(sock_path);

  if (sandbox2::IsLandlockSupported()) {
    builder.EnableLandlock(sandbox2::EnableLandlock()).AddFile(path);
  }

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, builder.TryBuild());

  Sandbox2 s2(std::move(executor), std::move(policy));
  ASSERT_TRUE(s2.RunAsync());

  ASSERT_TRUE(s2.comms()->SendInt32(-1));

  int client_sock = accept(host_sock, nullptr, nullptr);
  ASSERT_GE(client_sock, 0);
  sapi::file_util::fileops::FDCloser client_closer(client_sock);

  char buf[128] = {0};
  ssize_t n = read(client_sock, buf, sizeof(buf) - 1);
  EXPECT_GT(n, 0);
  EXPECT_EQ(std::string(buf), "Hello Unix Connect\n");

  sandbox2::Result result = s2.AwaitResult();
  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(EXIT_SUCCESS));
  unlink(sock_path.c_str());
}

TEST(NetworkProxyTest, ProxyConnectUnixSocketNotAllowed) {
  SKIP_SANITIZERS;
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/network_proxy");
  const std::string sock_path = CreateTempUnixSocketPath();

  int host_sock = socket(AF_UNIX, SOCK_STREAM, 0);
  ASSERT_GE(host_sock, 0);
  sapi::file_util::fileops::FDCloser host_closer(host_sock);

  struct sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);
  socklen_t addr_len =
      offsetof(struct sockaddr_un, sun_path) + strlen(addr.sun_path) + 1;
  ASSERT_EQ(
      bind(host_sock, reinterpret_cast<struct sockaddr*>(&addr), addr_len), 0);
  ASSERT_EQ(listen(host_sock, 1), 0);

  std::vector<std::string> args = {
      "network_proxy",
      "--test_connect_unix",
      absl::StrCat("--unix_socket_path=", sock_path),
  };
  auto executor = std::make_unique<Executor>(path, args);
  executor->limits()->set_walltime_limit(absl::Seconds(3));

  PolicyBuilder builder;
  builder.AllowDynamicStartup(sandbox2::MapExec())
      .AllowExit()
      .AllowWrite()
      .AllowRead()
      .AllowTcMalloc()
      .AddNetworkProxyHandlerPolicy(/*filter_unix_sockets=*/true)
      .AllowLlvmCoverage()
      .AddLibrariesForBinary(path)
      .AddDirectory("/sys", false)
      .AddDirectory("/proc", false)
      .AllowUnixSocket("/tmp/some_other_allowed_socket");

  if (sandbox2::IsLandlockSupported()) {
    builder.EnableLandlock(sandbox2::EnableLandlock()).AddFile(path);
  }

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, builder.TryBuild());

  Sandbox2 s2(std::move(executor), std::move(policy));
  ASSERT_TRUE(s2.RunAsync());

  ASSERT_TRUE(s2.comms()->SendInt32(-1));

  sandbox2::Result result = s2.AwaitResult();
  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(Result::VIOLATION_NETWORK));
  unlink(sock_path.c_str());
}

TEST(NetworkProxyTest, ProxyDatagramUnixSocketBlocked) {
  SKIP_SANITIZERS;
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/network_proxy");

  std::vector<std::string> args = {
      "network_proxy",
      "--test_dgram_unix_blocked",
  };
  auto executor = std::make_unique<Executor>(path, args);
  executor->limits()->set_walltime_limit(absl::Seconds(3));

  PolicyBuilder builder;
  builder.AllowDynamicStartup(sandbox2::MapExec())
      .AllowExit()
      .AllowWrite()
      .AllowRead()
      .AllowTcMalloc()
      .AddNetworkProxyHandlerPolicy(/*filter_unix_sockets=*/true)
      .AllowLlvmCoverage()
      .AddLibrariesForBinary(path)
      .AddDirectory("/sys", false)
      .AddDirectory("/proc", false);

  if (sandbox2::IsLandlockSupported()) {
    builder.EnableLandlock(sandbox2::EnableLandlock()).AddFile(path);
  }

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, builder.TryBuild());

  Sandbox2 s2(std::move(executor), std::move(policy));
  ASSERT_TRUE(s2.RunAsync());

  ASSERT_TRUE(s2.comms()->SendInt32(-1));

  sandbox2::Result result = s2.AwaitResult();
  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(EXIT_SUCCESS));
}

INSTANTIATE_TEST_SUITE_P(NetworkProxyTest, NetworkProxyTest,
                         ::testing::Combine(::testing::Bool(),
                                            ::testing::Bool()));

}  // namespace
}  // namespace sandbox2
