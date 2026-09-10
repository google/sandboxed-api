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
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "sandboxed_api/examples/stringop/stringop-sapi.sapi.h"
#include "sandboxed_api/examples/zlib/zlib-sapi.sapi.h"
#include "sandboxed_api/sandbox.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/sandbox_pool_global.h"
#include "sandboxed_api/util/thread.h"
#include "sandboxed_api/vars.h"

namespace sapi {

namespace {

using ::absl_testing::IsOk;
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
    absl::MutexLock lock(factory_calls_mutex);
    EXPECT_EQ(factory_calls, 1);
  }
  {
    absl::MutexLock lock(factory_calls_mutex);
    auto predicate = [&factory_calls] { return factory_calls >= 2; };
    EXPECT_TRUE(factory_calls_mutex.AwaitWithTimeout(
        absl::Condition(&predicate), absl::Seconds(5)));
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

TEST(SandboxPoolTest, ThreadlessMode) {
  SandboxPoolOptions options;
  options.min_sandboxes = 2;
  options.max_sandboxes = 4;
  options.max_sandbox_reuse = 2;
  options.max_maintenance_threads = 0;

  SAPI_ASSERT_OK_AND_ASSIGN(auto pool,
                            SandboxPool<StringopSandbox>::Create(options));
  // In threadless mode, pre-warming happens synchronously during Create().
  EXPECT_EQ(pool->AvailableCount(), 2);

  StringopSandbox* raw_sbx = nullptr;
  {
    SAPI_ASSERT_OK_AND_ASSIGN(auto handle, pool->Acquire());
    raw_sbx = handle.get();
    EXPECT_NE(raw_sbx, nullptr);
    EXPECT_EQ(pool->AvailableCount(), 1);
  }
  // Usage count is 1 (< max_sandbox_reuse), so it returns to the idle queue.
  EXPECT_EQ(pool->AvailableCount(), 2);

  {
    SAPI_ASSERT_OK_AND_ASSIGN(auto handle1, pool->Acquire());
    SAPI_ASSERT_OK_AND_ASSIGN(auto handle2, pool->Acquire());
    EXPECT_EQ(pool->AvailableCount(), 0);
  }
  // One sandbox reached usage_count == 2 and was recycled synchronously;
  // the other reached usage_count == 1 and was returned to the idle queue.
  EXPECT_EQ(pool->AvailableCount(), 2);

  {
    SAPI_ASSERT_OK_AND_ASSIGN(auto handle1, pool->Acquire());
    EXPECT_EQ(pool->AvailableCount(), 1);
    // Idle queue is empty, so this Acquire creates a new sandbox on demand.
    SAPI_ASSERT_OK_AND_ASSIGN(auto handle2, pool->Acquire());
    EXPECT_NE(handle2.get(), nullptr);
  }
}

TEST(SandboxPoolTest, FailingFactoryReturnsErrorToUser) {
  SAPI_ASSERT_OK_AND_ASSIGN(auto pool,
                            SandboxPool<StringopSandbox>::Create({}, []() {
                              return absl::ResourceExhaustedError("test error");
                            }));
  EXPECT_THAT(pool->Acquire(), StatusIs(absl::StatusCode::kResourceExhausted));
}

std::shared_ptr<SandboxPool<StringopSandbox>> g_pool;

void BenchmarkSetup(const benchmark::State& state) {
  SandboxPoolOptions options;

  g_pool = SandboxPool<StringopSandbox>::Create(options).value();

  // Give workers a brief moment to pre-warm sandboxes
  absl::SleepFor(absl::Milliseconds(200));
}

void BenchmarkTeardown(const benchmark::State& state) { g_pool.reset(); }

void BM_HighConcurrencyHold(benchmark::State& state) {
  const int sandboxes_per_thread = state.range(0);

  std::vector<SandboxHandle<StringopSandbox>> held_handles;
  held_handles.reserve(sandboxes_per_thread);
  for (auto s : state) {
    for (int i = 0; i < sandboxes_per_thread; ++i) {
      SAPI_ASSERT_OK_AND_ASSIGN(auto handle, g_pool->Acquire());
      held_handles.push_back(std::move(handle));
    }
  }
  held_handles.clear();
  state.SetItemsProcessed(state.iterations() * sandboxes_per_thread);
}
BENCHMARK(BM_HighConcurrencyHold)
    ->Setup(BenchmarkSetup)
    ->Teardown(BenchmarkTeardown)
    ->Arg(25)
    ->Arg(50)
    ->Arg(100)
    ->Threads(32)
    ->Threads(64)
    ->UseRealTime();

// -----------------------------------------------------------------------------
// Real-world Zlib Compression & Decompression Benchmarks
// -----------------------------------------------------------------------------

// Zlib constants needed by the SAPI sandboxee (matches standard zlib
// definitions).
constexpr int kZFinish = 4;
constexpr int kZOk = 0;
constexpr int kZDefaultCompression = -1;
constexpr int kZStreamEnd = 1;
constexpr char kZlibVersion[] = "1.2.11";
constexpr size_t kZlibOutputCapacity = 32768;

absl::Status CompressPayload(sapi::zlib::ZlibSandbox* sandbox,
                             const unsigned char* in_data, size_t in_size,
                             unsigned char* out_data, size_t out_capacity,
                             size_t* out_size) {
  sapi::zlib::ZlibApi api(sandbox);
  sapi::v::Struct<sapi::zlib::z_stream> strm;

  sapi::v::Array<unsigned char> input(const_cast<unsigned char*>(in_data),
                                      in_size);
  sapi::v::Array<unsigned char> output(out_data, out_capacity);

  ABSL_RETURN_IF_ERROR(sandbox->Allocate(&input, true));
  ABSL_RETURN_IF_ERROR(sandbox->Allocate(&output, true));

  sapi::v::Array<const char> version(kZlibVersion, sizeof(kZlibVersion));

  strm.mutable_data()->avail_in = in_size;
  strm.mutable_data()->next_in =
      reinterpret_cast<unsigned char*>(input.GetRemote());
  strm.mutable_data()->avail_out = out_capacity;
  strm.mutable_data()->next_out =
      reinterpret_cast<unsigned char*>(output.GetRemote());

  ABSL_RETURN_IF_ERROR(sandbox->TransferToSandboxee(&input));

  ABSL_ASSIGN_OR_RETURN(
      int ret,
      api.deflateInit_(strm.PtrBoth(), kZDefaultCompression,
                       version.PtrBefore(), sizeof(sapi::zlib::z_stream)));
  if (ret != kZOk) {
    return absl::InternalError("deflateInit failed");
  }

  ABSL_ASSIGN_OR_RETURN(ret, api.deflate(strm.PtrBoth(), kZFinish));
  if (ret != kZStreamEnd) {
    api.deflateEnd(strm.PtrBoth()).IgnoreError();
    return absl::InternalError("deflate failed to finish in one chunk");
  }

  ABSL_RETURN_IF_ERROR(sandbox->TransferFromSandboxee(&output));
  *out_size = out_capacity - strm.data().avail_out;

  ABSL_RETURN_IF_ERROR(api.deflateEnd(strm.PtrBoth()).status());
  return absl::OkStatus();
}

absl::Status DecompressPayload(sapi::zlib::ZlibSandbox* sandbox,
                               const unsigned char* in_data, size_t in_size,
                               unsigned char* out_data, size_t out_capacity,
                               size_t* out_size) {
  sapi::zlib::ZlibApi api(sandbox);
  sapi::v::Struct<sapi::zlib::z_stream> strm;

  sapi::v::Array<unsigned char> input(const_cast<unsigned char*>(in_data),
                                      in_size);
  sapi::v::Array<unsigned char> output(out_data, out_capacity);

  ABSL_RETURN_IF_ERROR(sandbox->Allocate(&input, true));
  ABSL_RETURN_IF_ERROR(sandbox->Allocate(&output, true));

  sapi::v::Array<const char> version(kZlibVersion, sizeof(kZlibVersion));

  strm.mutable_data()->avail_in = in_size;
  strm.mutable_data()->next_in =
      reinterpret_cast<unsigned char*>(input.GetRemote());
  strm.mutable_data()->avail_out = out_capacity;
  strm.mutable_data()->next_out =
      reinterpret_cast<unsigned char*>(output.GetRemote());

  ABSL_RETURN_IF_ERROR(sandbox->TransferToSandboxee(&input));

  ABSL_ASSIGN_OR_RETURN(int ret,
                        api.inflateInit_(strm.PtrBoth(), version.PtrBefore(),
                                         sizeof(sapi::zlib::z_stream)));
  if (ret != kZOk) {
    return absl::InternalError("inflateInit failed");
  }

  ABSL_ASSIGN_OR_RETURN(ret, api.inflate(strm.PtrBoth(), kZFinish));
  if (ret != kZStreamEnd) {
    api.inflateEnd(strm.PtrBoth()).IgnoreError();
    return absl::InternalError("inflate failed to finish in one chunk");
  }

  ABSL_RETURN_IF_ERROR(sandbox->TransferFromSandboxee(&output));
  *out_size = out_capacity - strm.data().avail_out;

  ABSL_RETURN_IF_ERROR(api.inflateEnd(strm.PtrBoth()).status());
  return absl::OkStatus();
}

absl::Status CompressAndDecompressPayload(
    sapi::zlib::ZlibSandbox* sandbox, absl::string_view payload,
    std::vector<unsigned char>& compressed,
    std::vector<unsigned char>& decompressed) {
  size_t compressed_size = 0;
  ABSL_RETURN_IF_ERROR(CompressPayload(
      sandbox, reinterpret_cast<const unsigned char*>(payload.data()),
      payload.size(), compressed.data(), compressed.size(), &compressed_size));

  size_t decompressed_size = 0;
  ABSL_RETURN_IF_ERROR(DecompressPayload(
      sandbox, compressed.data(), compressed_size, decompressed.data(),
      decompressed.size(), &decompressed_size));

  if (decompressed_size != payload.size()) {
    return absl::InternalError(absl::StrCat("Decompressed size mismatch: got ",
                                            decompressed_size, ", expected ",
                                            payload.size()));
  }
  if (memcmp(decompressed.data(), payload.data(), payload.size()) != 0) {
    return absl::InternalError("Decompressed content mismatch");
  }
  return absl::OkStatus();
}

const std::string& GetZlibBenchmarkPayload() {
  static const std::string* const kPayload = []() {
    std::string s;
    s.reserve(16384);
    while (s.size() < 16384) {
      s.append(
          "{\"event\":\"benchmark_event\",\"user_id\":42,"
          "\"status\":\"SUCCESS\",\"data\":\"Repeated structured JSON payload "
          "to simulate real compression data\"}\n");
    }
    s.resize(16384);
    return new std::string(std::move(s));
  }();
  return *kPayload;
}

inline sapi::zlib::ZlibSandbox* ToSandboxPtr(
    const std::unique_ptr<sapi::zlib::ZlibSandbox>& ptr) {
  return ptr.get();
}

inline sapi::zlib::ZlibSandbox* ToSandboxPtr(
    const SandboxHandle<sapi::zlib::ZlibSandbox>& handle) {
  return handle.get();
}

inline sapi::zlib::ZlibSandbox* ToSandboxPtr(sapi::zlib::ZlibSandbox& ref) {
  return &ref;
}

template <typename AcquireFn>
void RunZlibBenchmark(benchmark::State& state, AcquireFn&& acquire_fn) {
  const std::string& payload = GetZlibBenchmarkPayload();
  std::vector<unsigned char> compressed(kZlibOutputCapacity);
  std::vector<unsigned char> decompressed(kZlibOutputCapacity);

  for (auto s : state) {
    SAPI_ASSERT_OK_AND_ASSIGN(auto&& sbx_handle, acquire_fn());
    sapi::zlib::ZlibSandbox* sandbox = ToSandboxPtr(sbx_handle);
    ASSERT_THAT(CompressAndDecompressPayload(sandbox, payload, compressed,
                                             decompressed),
                IsOk());
    benchmark::DoNotOptimize(compressed[0]);
    benchmark::DoNotOptimize(decompressed[0]);
  }
  state.SetBytesProcessed(state.iterations() * payload.size());
}

std::shared_ptr<SandboxPool<sapi::zlib::ZlibSandbox>> SetupZlibPool(
    size_t max_sandbox_reuse = 0) {
  SandboxPoolOptions options;
  if (max_sandbox_reuse > 0) {
    options.max_sandbox_reuse = max_sandbox_reuse;
  }
  auto pool = SandboxPool<sapi::zlib::ZlibSandbox>::Create(options).value();
  absl::SleepFor(absl::Milliseconds(200));
  return pool;
}

std::shared_ptr<SandboxPool<sapi::zlib::ZlibSandbox>> g_zlib_pool;

void ZlibBenchmarkSetup(const benchmark::State& state) {
  g_zlib_pool = SetupZlibPool(50);
}

void ZlibBenchmarkTeardown(const benchmark::State& state) {
  g_zlib_pool.reset();
}

void BM_Zlib_ManualSpawn(benchmark::State& state) {
  RunZlibBenchmark(
      state, []() { return sapi::MakeSandbox<sapi::zlib::ZlibSandbox>(); });
}
BENCHMARK(BM_Zlib_ManualSpawn)->UseRealTime()->ThreadRange(1, 64);

void BM_Zlib_SandboxPool(benchmark::State& state) {
  RunZlibBenchmark(state, []() { return g_zlib_pool->Acquire(); });
}
BENCHMARK(BM_Zlib_SandboxPool)
    ->Setup(ZlibBenchmarkSetup)
    ->Teardown(ZlibBenchmarkTeardown)
    ->UseRealTime()
    ->ThreadRange(1, 64);

std::shared_ptr<SandboxPool<sapi::zlib::ZlibSandbox>> g_zlib_no_reuse_pool;

void ZlibNoReuseBenchmarkSetup(const benchmark::State& state) {
  g_zlib_no_reuse_pool = SetupZlibPool(1);
}

void ZlibNoReuseBenchmarkTeardown(const benchmark::State& state) {
  g_zlib_no_reuse_pool.reset();
}

void BM_Zlib_SandboxPool_NoReuse(benchmark::State& state) {
  RunZlibBenchmark(state, []() { return g_zlib_no_reuse_pool->Acquire(); });
}
BENCHMARK(BM_Zlib_SandboxPool_NoReuse)
    ->Setup(ZlibNoReuseBenchmarkSetup)
    ->Teardown(ZlibNoReuseBenchmarkTeardown)
    ->UseRealTime()
    ->ThreadRange(1, 64);

}  // namespace

}  // namespace sapi
