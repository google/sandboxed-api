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

#include <syscall.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/time/time.h"
#include "sandboxed_api/sandbox2/allowlists/map_exec.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/network_proxy/testing.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/testing.h"

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

INSTANTIATE_TEST_SUITE_P(NetworkProxyTest, NetworkProxyTest,
                         ::testing::Combine(::testing::Bool(),
                                            ::testing::Bool()));

}  // namespace
}  // namespace sandbox2
