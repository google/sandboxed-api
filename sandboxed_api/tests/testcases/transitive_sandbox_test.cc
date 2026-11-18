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

#include <unistd.h>

#include "gtest/gtest.h"

#if defined(TEST_SAPI_SANDBOX2) || defined(TEST_SAPI_PASSTHROUGH)
#include "gmock/gmock.h"
#include "absl/status/status_matchers.h"
#endif

#if !defined(TEST_SAPI_SANDBOX2)
#include "sandboxed_api/config.h"
#endif

#if defined(TEST_SAPI_SANDBOX2)
#include "sandboxed_api/tests/testcases/transitive_outer_sapi.sapi.h"
#elif defined(TEST_SAPI_PASSTHROUGH)
#include "sandboxed_api/tests/testcases/transitive_outer_passthrough_sapi.sapi.h"
#elif defined(TEST_LWBOX_ONLY)
#include "sandboxed_api/tests/testcases/transitive_outer.h"
#elif defined(TEST_LWBOX_BOTH)
#include "sandboxed_api/tests/testcases/transitive_inner.h"
#include "sandboxed_api/tests/testcases/transitive_outer.h"
#else
#error "Must define a TEST_* mode macro"
#endif

namespace {

#if defined(TEST_SAPI_SANDBOX2) || defined(TEST_SAPI_PASSTHROUGH)
using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
#endif

#if defined(TEST_SAPI_SANDBOX2)
TEST(TransitiveSandboxTest, SapiSandbox2RunsTransitiveSandboxedDepInProcess) {
  const int host_pid = getpid();
  sapi_transitive_test::TransitiveOuterSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk());
  sapi_transitive_test::TransitiveOuterApi api(&sandbox);

  EXPECT_THAT(api.transitive_outer_add(20, 22), IsOkAndHolds(142));
  EXPECT_THAT(api.transitive_outer_is_sandboxed(), IsOkAndHolds(true));
  EXPECT_THAT(api.transitive_outer_inner_is_sandboxed(), IsOkAndHolds(true));

  ASSERT_OK_AND_ASSIGN(const int outer_pid, api.transitive_outer_pid());
  ASSERT_OK_AND_ASSIGN(const int inner_pid, api.transitive_outer_inner_pid());
  EXPECT_NE(outer_pid, host_pid);
  EXPECT_EQ(inner_pid, outer_pid);
}
#elif defined(TEST_SAPI_PASSTHROUGH)
TEST(TransitiveSandboxTest, SapiPassthroughSandboxesTransitiveSandboxedDep) {
  constexpr bool kExpectSandboxed = !sapi::sanitizers::IsTSan();
  const int host_pid = getpid();
  sapi_transitive_passthrough_test::TransitiveOuterPassthroughSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk());
  sapi_transitive_passthrough_test::TransitiveOuterPassthroughApi api(&sandbox);

  EXPECT_THAT(api.transitive_outer_add(20, 22), IsOkAndHolds(142));
  EXPECT_THAT(api.transitive_outer_is_sandboxed(), IsOkAndHolds(false));
  EXPECT_THAT(api.transitive_outer_inner_is_sandboxed(),
              IsOkAndHolds(kExpectSandboxed));

  ASSERT_OK_AND_ASSIGN(const int outer_pid, api.transitive_outer_pid());
  ASSERT_OK_AND_ASSIGN(const int inner_pid, api.transitive_outer_inner_pid());
  EXPECT_EQ(outer_pid, host_pid);
  if constexpr (kExpectSandboxed) {
    EXPECT_NE(inner_pid, host_pid);
  } else {
    EXPECT_EQ(inner_pid, host_pid);
  }
}
#elif defined(TEST_LWBOX_ONLY)
TEST(TransitiveSandboxTest, SandboxedLibraryTransitiveDepRunsInSameSandbox) {
  constexpr bool kExpectSandboxed = !sapi::sanitizers::IsTSan();
  const int host_pid = getpid();
  EXPECT_EQ(transitive_outer_add(20, 22), 142);
  EXPECT_EQ(transitive_outer_is_sandboxed(), kExpectSandboxed);
  EXPECT_EQ(transitive_outer_inner_is_sandboxed(), kExpectSandboxed);

  const int outer_pid = transitive_outer_pid();
  const int inner_pid = transitive_outer_inner_pid();
  if constexpr (kExpectSandboxed) {
    EXPECT_NE(outer_pid, host_pid);
  } else {
    EXPECT_EQ(outer_pid, host_pid);
  }
  EXPECT_EQ(inner_pid, outer_pid);
}
#elif defined(TEST_LWBOX_BOTH)
TEST(TransitiveSandboxTest,
     DirectAndTransitiveSandboxedLibrariesShareMergedSandbox) {
  constexpr bool kExpectSandboxed = !sapi::sanitizers::IsTSan();
  const int host_pid = getpid();
  EXPECT_EQ(transitive_inner_add(10, 5), 15);
  EXPECT_EQ(transitive_outer_add(20, 22), 142);
  EXPECT_EQ(transitive_inner_is_sandboxed(), kExpectSandboxed);
  EXPECT_EQ(transitive_outer_is_sandboxed(), kExpectSandboxed);
  EXPECT_EQ(transitive_outer_inner_is_sandboxed(), kExpectSandboxed);

  const int direct_inner_pid = transitive_inner_pid();
  const int outer_pid = transitive_outer_pid();
  const int nested_inner_pid = transitive_outer_inner_pid();
  if constexpr (kExpectSandboxed) {
    EXPECT_NE(outer_pid, host_pid);
  } else {
    EXPECT_EQ(outer_pid, host_pid);
  }
  EXPECT_EQ(direct_inner_pid, outer_pid);
  EXPECT_EQ(nested_inner_pid, outer_pid);
}
#endif

}  // namespace
