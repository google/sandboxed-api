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

#include "sandboxed_api/util/sandbox_pool.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "sandboxed_api/examples/stringop/stringop-sapi.sapi.h"
#include "sandboxed_api/sandbox.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/sandbox_pool_global.h"
#include "sandboxed_api/util/thread.h"

namespace sapi {

namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::Ge;

TEST(SandboxPoolTest, AcquireManyWorks) {
  SandboxPoolOptions options;
  options.min_sandboxes = 1;
  options.max_sandboxes = 10;
  SAPI_ASSERT_OK_AND_ASSIGN(auto pool,
                            SandboxPool<StringopSandbox>::Create(options));
  std::vector<SandboxHandle<StringopSandbox>> handles;
  handles.reserve(10);
  for (size_t i = 0; i < 10; ++i) {
    SAPI_ASSERT_OK_AND_ASSIGN(auto handle, pool->Acquire());
    handles.push_back(std::move(handle));
  }
}

TEST(SandboxPoolTest, ReleaseWorks) {
  SandboxPoolOptions options;
  options.min_sandboxes = 0;
  options.max_sandboxes = 1;
  SAPI_ASSERT_OK_AND_ASSIGN(auto pool,
                            SandboxPool<StringopSandbox>::Create(options));
  {
    SAPI_ASSERT_OK_AND_ASSIGN(auto handle, pool->Acquire());
  }
  {
    // This should work because we released the previous sandbox.
    SAPI_ASSERT_OK_AND_ASSIGN(auto handle, pool->Acquire());
  }
}

TEST(SandboxPoolTest, ReuseOnlyCreatesOneSandbox) {
  SandboxPoolOptions options;
  options.min_sandboxes = 1;
  options.max_sandboxes = 5;
  options.max_sandbox_reuse = 2;
  size_t factory_calls = 0;
  absl::Mutex factory_calls_mutex;
  auto factory = [&factory_calls, &factory_calls_mutex]() {
    absl::MutexLock lock(factory_calls_mutex);
    factory_calls++;
    return sapi::MakeSandbox<StringopSandbox>();
  };
  SAPI_ASSERT_OK_AND_ASSIGN(
      auto pool, SandboxPool<StringopSandbox>::Create(options, factory));
  {
    absl::MutexLock lock(factory_calls_mutex);
    auto predicate = [&factory_calls] { return factory_calls >= 1; };
    EXPECT_TRUE(factory_calls_mutex.AwaitWithTimeout(
        absl::Condition(&predicate), absl::Seconds(5)));
  }
  {
    SAPI_ASSERT_OK_AND_ASSIGN(auto handle1, pool->Acquire());
  }
  {
    SAPI_ASSERT_OK_AND_ASSIGN(auto handle2, pool->Acquire());
  }
  {
    absl::MutexLock lock(factory_calls_mutex);
    EXPECT_EQ(factory_calls, 1);
  }
}

TEST(SandboxPoolTest, AcquireWithExhaustedPoolWorks) {
  SandboxPoolOptions options;
  options.min_sandboxes = 1;
  options.max_sandboxes = 1;
  SAPI_ASSERT_OK_AND_ASSIGN(auto pool,
                            SandboxPool<StringopSandbox>::Create(options));
  absl::SleepFor(absl::Milliseconds(100));
  SAPI_ASSERT_OK_AND_ASSIGN(auto handle1, pool->Acquire());
  EXPECT_THAT(pool->Acquire(absl::Milliseconds(10)),
              StatusIs(absl::StatusCode::kDeadlineExceeded));
}

TEST(SandboxPoolTest, BackgroundCreationWorks) {
  SandboxPoolOptions options;
  options.min_sandboxes = 4;
  options.max_sandboxes = 20;
  size_t factory_calls = 0;
  absl::Mutex factory_calls_mutex;
  auto factory = [&factory_calls, &factory_calls_mutex]() {
    absl::MutexLock lock(factory_calls_mutex);
    ++factory_calls;
    auto sandbox = sapi::MakeSandbox<StringopSandbox>();
    return sandbox;
  };
  SAPI_ASSERT_OK_AND_ASSIGN(
      auto pool, SandboxPool<StringopSandbox>::Create(options, factory));
  {
    absl::MutexLock lock(factory_calls_mutex);
    auto predicate = [&factory_calls] { return factory_calls >= 4; };
    EXPECT_TRUE(factory_calls_mutex.AwaitWithTimeout(
        absl::Condition(&predicate), absl::Seconds(5)));
  }
  SAPI_ASSERT_OK_AND_ASSIGN(auto handle1, pool->Acquire());
  {
    absl::MutexLock lock(factory_calls_mutex);
    EXPECT_EQ(factory_calls, 4);
  }
}

TEST(SandboxPoolTest, ExpiredSandboxesAreDestroyed) {
  SandboxPoolOptions options;
  options.min_sandboxes = 1;
  options.max_sandboxes = 20;
  options.idle_timeout = absl::Milliseconds(5);
  SAPI_ASSERT_OK_AND_ASSIGN(auto pool,
                            SandboxPool<StringopSandbox>::Create(options));
  std::vector<SandboxHandle<StringopSandbox>> handles;
  handles.reserve(5);
  for (size_t i = 0; i < 5; ++i) {
    SAPI_ASSERT_OK_AND_ASSIGN(auto handle, pool->Acquire());
    handles.push_back(std::move(handle));
  }
  handles.clear();
  absl::SleepFor(absl::Milliseconds(100));
#if defined(THREAD_SANITIZER)
  // In TSAN, to avoid flaky tests, we just check that the pool has fewer
  // sandboxes than the initial number of sandboxes we created.
  EXPECT_LT(pool->AvailableCount(), 5);
#else
  EXPECT_EQ(pool->AvailableCount(), 1);
#endif
}

TEST(SandboxPoolTest, GlobalPoolWorks) {
  SAPI_ASSERT_OK_AND_ASSIGN(auto handle1,
                            state::AcquirePooledSandbox<StringopSandbox>());
  SAPI_ASSERT_OK_AND_ASSIGN(auto handle2,
                            state::AcquirePooledSandbox<StringopSandbox>());
  SAPI_ASSERT_OK_AND_ASSIGN(auto handle3,
                            state::AcquirePooledSandbox<StringopSandbox>());
  SAPI_ASSERT_OK_AND_ASSIGN(auto handle4,
                            state::AcquirePooledSandbox<StringopSandbox>());
}

TEST(SandboxPoolTest, WrongPoolOptions) {
  EXPECT_THAT(SandboxPool<StringopSandbox>::Create({
                  .min_sandboxes = 2,
                  .max_sandboxes = 1,
              }),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(SandboxPool<StringopSandbox>::Create({
                  .min_sandboxes = 0,
                  .max_sandboxes = 0,
              }),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(SandboxPoolTest, FailingFactory) {
  SAPI_ASSERT_OK_AND_ASSIGN(
      auto pool, SandboxPool<StringopSandbox>::Create(
                     {
                         .min_sandboxes = 1,
                         .max_sandboxes = 10,
                     },
                     []() { return absl::InternalError("test error"); }));
  EXPECT_THAT(pool->Acquire(), StatusIs(absl::StatusCode::kInternal));
}

TEST(SandboxPoolTest, HandleOutlivesPool) {
  SandboxPoolOptions options;
  options.min_sandboxes = 1;
  options.max_sandboxes = 2;

  SAPI_ASSERT_OK_AND_ASSIGN(auto pool,
                            SandboxPool<StringopSandbox>::Create(options));
  SAPI_ASSERT_OK_AND_ASSIGN(auto handle, pool->Acquire());
  EXPECT_NE(handle.get(), nullptr);

  // Drop the user's reference to the pool.
  pool.reset();

  // The pool is kept alive by handle's internal shared_ptr. The handle should
  // still be usable.
  StringopApi api(handle.get());
  SAPI_ASSERT_OK_AND_ASSIGN(auto ptr, api.get_raw_c_string());
  EXPECT_NE(ptr, nullptr);
}

std::shared_ptr<SandboxPool<StringopSandbox>> g_pool;

void BenchmarkSetup(const benchmark::State& state) {
  SandboxPoolOptions options;
  options.max_maintenance_threads = 32;

  g_pool = SandboxPool<StringopSandbox>::Create(options).value();

  // Give the 16 workers a moment to fully pre-warm the 64 sandboxes
  absl::SleepFor(absl::Seconds(2));
}

void BenchmarkTeardown(const benchmark::State& state) { g_pool.reset(); }

void BM_AcquireRelease(benchmark::State& state) {
  for (auto s : state) {
    auto handle = g_pool->Acquire().value();
    StringopApi api(handle.get());
    benchmark::DoNotOptimize(api.get_raw_c_string());
  }
}
BENCHMARK(BM_AcquireRelease)
    ->Setup(BenchmarkSetup)
    ->Teardown(BenchmarkTeardown)
    ->UseRealTime()
    ->ThreadRange(1, 256);

}  // namespace

}  // namespace sapi
