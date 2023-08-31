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
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/network_proxy/testing.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sandbox2 {
namespace {

using ::sapi::GetTestSourcePath;
using ::testing::Eq;

TEST(NetworkProxy, ProxyWithHandlerAllowed) {
  SKIP_SANITIZERS;
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/network_proxy");
  std::vector<std::string> args = {"network_proxy"};
  auto executor = std::make_unique<Executor>(path, args);

  PolicyBuilder builder;
  builder.AllowDynamicStartup()
      .AllowWrite()
      .AllowRead()
      .AllowExit()
      .AllowSyscall(__NR_sendto)
      .AllowTcMalloc()
      .AddNetworkProxyHandlerPolicy()
      .AllowLlvmCoverage()
      .AllowIPv6("::1")
      .AddLibrariesForBinary(path);

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, builder.TryBuild());

  Sandbox2 s2(std::move(executor), std::move(policy));
  ASSERT_TRUE(s2.RunAsync());

  SAPI_ASSERT_OK_AND_ASSIGN(int port, StartNetworkProxyTestServer());
  ASSERT_TRUE(s2.comms()->SendInt32(port));

  sandbox2::Result result = s2.AwaitResult();
  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(EXIT_SUCCESS));
}

TEST(NetworkProxy, ProxyWithHandlerNotAllowed) {
  SKIP_SANITIZERS;
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/network_proxy");
  std::vector<std::string> args = {"network_proxy"};
  auto executor = std::make_unique<Executor>(path, args);

  PolicyBuilder builder;
  builder.AllowDynamicStartup()
      .AllowWrite()
      .AllowRead()
      .AllowExit()
      .AllowSyscall(__NR_sendto)
      .AllowTcMalloc()
      .AddNetworkProxyHandlerPolicy()
      .AllowLlvmCoverage()
      .AddLibrariesForBinary(path);

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, builder.TryBuild());

  Sandbox2 s2(std::move(executor), std::move(policy));
  ASSERT_TRUE(s2.RunAsync());

  SAPI_ASSERT_OK_AND_ASSIGN(int port, StartNetworkProxyTestServer());
  ASSERT_TRUE(s2.comms()->SendInt32(port));

  sandbox2::Result result = s2.AwaitResult();
  ASSERT_THAT(result.final_status(), Eq(Result::VIOLATION));
  EXPECT_THAT(result.reason_code(), Eq(Result::VIOLATION_NETWORK));
}

TEST(NetworkProxy, ProxyWithoutHandlerAllowed) {
  SKIP_SANITIZERS;
  const std::string path =
      GetTestSourcePath("sandbox2/testcases/network_proxy");
  std::vector<std::string> args = {"network_proxy", "--noconnect_with_handler"};
  auto executor = std::make_unique<Executor>(path, args);

  PolicyBuilder builder;
  builder.AllowDynamicStartup()
      .AllowExit()
      .AllowWrite()
      .AllowRead()
      .AllowSyscall(__NR_sendto)
      .AllowTcMalloc()
      .AddNetworkProxyHandlerPolicy()
      .AllowLlvmCoverage()
      .AllowIPv6("::1")
      .AddLibrariesForBinary(path);

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, builder.TryBuild());

  Sandbox2 s2(std::move(executor), std::move(policy));
  ASSERT_TRUE(s2.RunAsync());

  SAPI_ASSERT_OK_AND_ASSIGN(int port, StartNetworkProxyTestServer());
  ASSERT_TRUE(s2.comms()->SendInt32(port));

  sandbox2::Result result = s2.AwaitResult();
  ASSERT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(EXIT_SUCCESS));
}

}  // namespace
}  // namespace sandbox2
