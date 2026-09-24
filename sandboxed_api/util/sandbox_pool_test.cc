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

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
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

// Polls `pred` until it holds or `timeout` elapses. Returns the final value of
// `pred`. Used instead of a fixed sleep for state the pool reaches
// asynchronously, such as pre-warming or pruning.
template <typename Pred>
bool WaitFor(Pred pred, absl::Duration timeout = absl::Seconds(10)) {
  absl::Time deadline = absl::Now() + timeout;
  while (absl::Now() < deadline) {
    if (pred()) {
      return true;
    }
    absl::SleepFor(absl::Milliseconds(1));
  }
  return pred();
}

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

TEST(SandboxPoolTest, AcquireReturnsMostRecentlyReleasedSandbox) {
  // Threadless mode without pre-warming: releases reach the idle queue
  // synchronously and nothing else pushes to it, so the order below is
  // deterministic. `max_sandbox_reuse` is high enough that releasing never
  // recycles a sandbox, which would give the replacement a different address.
  SAPI_ASSERT_OK_AND_ASSIGN(auto pool, SandboxPool<StringopSandbox>::Create({
                                           .min_sandboxes = 0,
                                           .max_sandboxes = 3,
                                           .max_sandbox_reuse = 100,
                                           .max_maintenance_threads = 0,
                                       }));

  SAPI_ASSERT_OK_AND_ASSIGN(auto handle1, pool->Acquire());
  SAPI_ASSERT_OK_AND_ASSIGN(auto handle2, pool->Acquire());
  SAPI_ASSERT_OK_AND_ASSIGN(auto handle3, pool->Acquire());
  StringopSandbox* first = handle1.get();
  StringopSandbox* second = handle2.get();
  StringopSandbox* third = handle3.get();

  // Release oldest first, so that every acquisition below tells the two orders
  // apart. Letting the handles go out of scope instead would release them in
  // reverse declaration order, where LIFO and FIFO agree on the first one.
  handle1 = SandboxHandle<StringopSandbox>();
  handle2 = SandboxHandle<StringopSandbox>();
  handle3 = SandboxHandle<StringopSandbox>();

  // Each acquisition has to keep its handle: a temporary would be released
  // again straight away and handed right back to the next acquisition.
  SAPI_ASSERT_OK_AND_ASSIGN(auto reacquired1, pool->Acquire());
  EXPECT_EQ(reacquired1.get(), third);
  SAPI_ASSERT_OK_AND_ASSIGN(auto reacquired2, pool->Acquire());
  EXPECT_EQ(reacquired2.get(), second);
  SAPI_ASSERT_OK_AND_ASSIGN(auto reacquired3, pool->Acquire());
  EXPECT_EQ(reacquired3.get(), first);
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
  // Wait for pre-warming to reach the idle queue. Waiting on `factory_calls`
  // is not sufficient: it is incremented on entry to the factory, before the
  // sandbox exists and before it is pushed, so the `Acquire()` below could
  // miss it and create a second sandbox.
  ASSERT_TRUE(WaitFor([&pool] { return pool->AvailableCount() >= 1; }));
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
  ASSERT_TRUE(WaitFor([&pool] { return pool->AvailableCount() >= 1; }));
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
  // Wait for all four background sandboxes to reach the idle queue, not merely
  // for the factory to have been entered four times: `Acquire()` below would
  // otherwise miss them and create a fifth.
  ASSERT_TRUE(WaitFor([&pool] { return pool->AvailableCount() >= 4; }));
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
  // Keep recycling out of the picture: this test is about expiry, and the
  // default of never reusing a sandbox would have five concurrent recycles
  // racing the pruner inside the window we measure.
  options.max_sandbox_reuse = 10;
  SAPI_ASSERT_OK_AND_ASSIGN(auto pool,
                            SandboxPool<StringopSandbox>::Create(options));
  std::vector<SandboxHandle<StringopSandbox>> handles;
  handles.reserve(5);
  for (size_t i = 0; i < 5; ++i) {
    SAPI_ASSERT_OK_AND_ASSIGN(auto handle, pool->Acquire());
    handles.push_back(std::move(handle));
  }
  handles.clear();
  // The pruner must reap everything above `min_sandboxes`, and no further.
  EXPECT_TRUE(WaitFor([&pool] { return pool->AvailableCount() == 1; }));
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

class SandboxTracker;

// Stands in for a real sandbox. The tests below churn through hundreds of
// acquisitions to build up a recycling backlog, which would be far too slow
// with real sandboxees.
class FakeSandbox {
 public:
  explicit FakeSandbox(SandboxTracker* tracker);
  ~FakeSandbox();

  FakeSandbox(const FakeSandbox&) = delete;
  FakeSandbox& operator=(const FakeSandbox&) = delete;

  // Stands in for the work a caller would do with an acquired sandbox.
  void Use();

 private:
  SandboxTracker* tracker_;
  // Only ever touched by the thread currently holding this sandbox.
  int usage_count_ = 0;
};

// Observes the lifetime and the reuse of the fake sandboxes a pool creates.
class SandboxTracker {
 public:
  // A factory for `SandboxPool<FakeSandbox>::Create`. `spawn_time` emulates the
  // cost of starting a real sandboxee, which is what lets a backlog build up.
  SandboxPool<FakeSandbox>::Factory Factory(absl::Duration spawn_time) {
    return
        [this, spawn_time]() -> absl::StatusOr<std::unique_ptr<FakeSandbox>> {
          absl::SleepFor(spawn_time);
          return std::make_unique<FakeSandbox>(this);
        };
  }

  void OnCreated() {
    absl::MutexLock lock(mutex_);
    ++live_count_;
    if (live_count_ > max_live_count_) {
      max_live_count_ = live_count_;
    }
  }

  void OnDestroyed() {
    absl::MutexLock lock(mutex_);
    --live_count_;
  }

  void OnUsed(int usage_count) {
    absl::MutexLock lock(mutex_);
    if (usage_count > max_usage_count_) {
      max_usage_count_ = usage_count;
    }
  }

  size_t live_count() const {
    absl::MutexLock lock(mutex_);
    return live_count_;
  }

  // The most sandboxes that ever existed at the same time.
  size_t max_live_count() const {
    absl::MutexLock lock(mutex_);
    return max_live_count_;
  }

  // The most times any single sandbox was handed out.
  int max_usage_count() const {
    absl::MutexLock lock(mutex_);
    return max_usage_count_;
  }

 private:
  mutable absl::Mutex mutex_;
  size_t live_count_ = 0;
  size_t max_live_count_ = 0;
  int max_usage_count_ = 0;
};

FakeSandbox::FakeSandbox(SandboxTracker* tracker) : tracker_(tracker) {
  tracker_->OnCreated();
}

FakeSandbox::~FakeSandbox() { tracker_->OnDestroyed(); }

void FakeSandbox::Use() { tracker_->OnUsed(++usage_count_); }

// Acquires and releases a sandbox `acquisitions` times on each of `threads`
// threads. The timeout is generous, but finite: a pool that stops replacing the
// sandboxes it recycles would otherwise hang the test rather than fail it.
void HammerPool(SandboxPool<FakeSandbox>& pool, size_t threads,
                size_t acquisitions) {
  std::vector<sapi::Thread> workers;
  workers.reserve(threads);
  for (size_t i = 0; i < threads; ++i) {
    workers.emplace_back(
        [&pool, acquisitions]() {
          for (size_t j = 0; j < acquisitions; ++j) {
            SAPI_ASSERT_OK_AND_ASSIGN(auto handle,
                                      pool.Acquire(absl::Seconds(30)));
            handle->Use();
          }
        },
        "sandbox_pool_hammer");
  }
  for (sapi::Thread& worker : workers) {
    worker.Join();
  }
}

TEST(SandboxPoolTest, RecycleBacklogDoesNotGrowThePool) {
  constexpr size_t kThreads = 8;
  constexpr size_t kAcquisitionsPerThread = 50;
  constexpr size_t kMaintenanceThreads = 1;

  SandboxTracker tracker;
  SAPI_ASSERT_OK_AND_ASSIGN(
      auto pool, SandboxPool<FakeSandbox>::Create(
                     {
                         .min_sandboxes = 1,
                         // Recycle on every release, so that the single
                         // maintenance thread cannot keep up.
                         .max_sandbox_reuse = 1,
                         .max_maintenance_threads = kMaintenanceThreads,
                     },
                     tracker.Factory(absl::Milliseconds(5))));

  HammerPool(*pool, kThreads, kAcquisitionsPerThread);

  // At any instant the pool should hold at most one sandbox per caller, plus
  // the idle floor, plus the recycles it allows itself to have in flight: two
  // queued and one running per maintenance thread. The bound is doubled to
  // cover sandboxes whose destructor has not returned yet and general
  // scheduling noise; what matters is the order of magnitude, as without the
  // bounded backlog this grows by one per recycle, which is several hundred
  // here and unbounded in production.
  constexpr size_t kRecyclesInFlight = 3 * kMaintenanceThreads;
  EXPECT_LE(tracker.max_live_count(), 2 * (kThreads + 1 + kRecyclesInFlight));
}

TEST(SandboxPoolTest, MinSandboxesGreaterThanTwiceWorkersPreservesFloor) {
  constexpr size_t kMinSandboxes = 6;

  SandboxTracker tracker;
  SAPI_ASSERT_OK_AND_ASSIGN(auto pool,
                            SandboxPool<FakeSandbox>::Create(
                                {
                                    .min_sandboxes = kMinSandboxes,
                                    .max_sandbox_reuse = 1,
                                    .max_maintenance_threads = 1,
                                },
                                tracker.Factory(absl::Milliseconds(2))));

  // All initial CreateTask items must fit in worker_queue_ and pre-warm the
  // pool to kMinSandboxes even though kMinSandboxes > 2 * maintenance_threads.
  ASSERT_TRUE(
      WaitFor([&pool] { return pool->AvailableCount() >= kMinSandboxes; }));

  HammerPool(*pool, /*threads=*/8, /*acquisitions=*/25);

  // Once the recycle backlog drains, the idle queue must still recover to at
  // least kMinSandboxes.
  EXPECT_TRUE(
      WaitFor([&pool] { return pool->AvailableCount() >= kMinSandboxes; }));
}

TEST(SandboxPoolTest, ReuseLimitHoldsWhenRecycleBacklogIsFull) {
  constexpr int kMaxSandboxReuse = 3;

  SandboxTracker tracker;
  SAPI_ASSERT_OK_AND_ASSIGN(auto pool,
                            SandboxPool<FakeSandbox>::Create(
                                {
                                    .min_sandboxes = 1,
                                    .max_sandbox_reuse = kMaxSandboxReuse,
                                    .max_maintenance_threads = 1,
                                },
                                tracker.Factory(absl::Milliseconds(5))));

  HammerPool(*pool, /*threads=*/8, /*acquisitions=*/50);

  // Dropping a sandbox instead of recycling it must never postpone the recycle:
  // the reuse limit is a guarantee, not a target.
  EXPECT_LE(tracker.max_usage_count(), kMaxSandboxReuse);
}

TEST(SandboxPoolTest, BoundedPoolMakesProgressWhenRecyclesAreDropped) {
  constexpr size_t kMaxSandboxes = 4;

  SandboxTracker tracker;
  SAPI_ASSERT_OK_AND_ASSIGN(auto pool,
                            SandboxPool<FakeSandbox>::Create(
                                {
                                    .min_sandboxes = 1,
                                    .max_sandboxes = kMaxSandboxes,
                                    .max_sandbox_reuse = 1,
                                    .max_maintenance_threads = 1,
                                },
                                tracker.Factory(absl::Milliseconds(5))));

  // With more callers than sandboxes, this keeps the recycling backlog full, so
  // releases routinely drop their sandbox rather than pushing a replacement --
  // something only pools without a maximum used to do. The pool must still
  // respect its maximum and still serve everyone: a dropped sandbox leaves a
  // free slot behind, and whoever needs it has to find it.
  HammerPool(*pool, /*threads=*/8, /*acquisitions=*/50);

  EXPECT_LE(tracker.max_live_count(), kMaxSandboxes);
}

TEST(SandboxPoolTest, FailedRecycleDoesNotStrandWaiter) {
  SandboxTracker tracker;
  std::atomic<bool> fail_next_creation = false;
  SAPI_ASSERT_OK_AND_ASSIGN(
      auto pool,
      SandboxPool<FakeSandbox>::Create(
          {
              .min_sandboxes = 1,
              // A single sandbox, so that the waiter below has to be handed
              // capacity rather than being able to create its own.
              .max_sandboxes = 1,
              .max_sandbox_reuse = 1,
              .max_maintenance_threads = 1,
          },
          [&tracker, &fail_next_creation]()
              -> absl::StatusOr<std::unique_ptr<FakeSandbox>> {
            if (fail_next_creation.exchange(false)) {
              return absl::InternalError("factory failed");
            }
            return std::make_unique<FakeSandbox>(&tracker);
          }));
  SAPI_ASSERT_OK_AND_ASSIGN(auto handle, pool->Acquire(absl::Seconds(30)));

  absl::Duration waited;
  absl::Status waiter_status;
  sapi::Thread waiter(
      [&pool, &waited, &waiter_status]() {
        absl::Time start = absl::Now();
        waiter_status = pool->Acquire(absl::Seconds(30)).status();
        waited = absl::Now() - start;
      },
      "sandbox_pool_waiter");

  // Give the waiter time to park: the pool is at its maximum size, so it cannot
  // create a sandbox of its own.
  absl::SleepFor(absl::Milliseconds(200));

  // Retire the only sandbox and make its replacement fail. The recycle gives
  // the slot back without pushing anything, which used to leave the waiter
  // blocked for the whole timeout even though the pool now had room for it.
  fail_next_creation = true;
  handle = SandboxHandle<FakeSandbox>();
  waiter.Join();

  EXPECT_THAT(waiter_status, IsOk());
  EXPECT_LT(waited, absl::Seconds(5));
}

TEST(SandboxPoolTest, ThreadlessModeAlwaysReplacesRecycledSandboxes) {
  SandboxTracker tracker;
  SAPI_ASSERT_OK_AND_ASSIGN(auto pool,
                            SandboxPool<FakeSandbox>::Create(
                                {
                                    .min_sandboxes = 1,
                                    .max_sandbox_reuse = 1,
                                    .max_maintenance_threads = 0,
                                },
                                tracker.Factory(absl::ZeroDuration())));
  ASSERT_EQ(pool->AvailableCount(), 1);

  // Recycling is synchronous here, so there is no backlog to bound and the
  // backlog bound must not apply: the pool is back to its idle floor by
  // the time the release returns.
  for (int i = 0; i < 5; ++i) {
    {
      SAPI_ASSERT_OK_AND_ASSIGN(auto handle, pool->Acquire());
      handle->Use();
      EXPECT_EQ(pool->AvailableCount(), 0);
    }
    EXPECT_EQ(pool->AvailableCount(), 1);
  }
  EXPECT_EQ(tracker.live_count(), 1);
  EXPECT_EQ(tracker.max_usage_count(), 1);
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
    size_t max_sandbox_reuse) {
  SandboxPoolOptions options;
  options.max_sandbox_reuse = max_sandbox_reuse;
  auto pool = SandboxPool<sapi::zlib::ZlibSandbox>::Create(options).value();
  absl::SleepFor(absl::Milliseconds(200));
  return pool;
}

std::shared_ptr<SandboxPool<sapi::zlib::ZlibSandbox>> g_zlib_pool;

template <size_t kMaxSandboxReuse>
void ZlibPoolSetup(const benchmark::State& state) {
  g_zlib_pool = SetupZlibPool(kMaxSandboxReuse);
}

void ZlibPoolTeardown(const benchmark::State& state) { g_zlib_pool.reset(); }

// Spawns a throwaway sandbox per call, so it never reuses by construction.
void BM_Zlib_ManualSpawn(benchmark::State& state) {
  RunZlibBenchmark(
      state, []() { return sapi::MakeSandbox<sapi::zlib::ZlibSandbox>(); });
}
BENCHMARK(BM_Zlib_ManualSpawn)->UseRealTime()->ThreadRange(1, 64);

// The three pool benchmarks below differ only in `max_sandbox_reuse`, so that
// the comparison between them isolates the reuse policy.
void BM_Zlib_SandboxPool_NeverReuse(benchmark::State& state) {
  RunZlibBenchmark(state, []() { return g_zlib_pool->Acquire(); });
}
BENCHMARK(BM_Zlib_SandboxPool_NeverReuse)
    ->Setup(ZlibPoolSetup<1>)
    ->Teardown(ZlibPoolTeardown)
    ->UseRealTime()
    ->ThreadRange(1, 64);

void BM_Zlib_SandboxPool_Reuse50(benchmark::State& state) {
  RunZlibBenchmark(state, []() { return g_zlib_pool->Acquire(); });
}
BENCHMARK(BM_Zlib_SandboxPool_Reuse50)
    ->Setup(ZlibPoolSetup<50>)
    ->Teardown(ZlibPoolTeardown)
    ->UseRealTime()
    ->ThreadRange(1, 64);

// Sandboxes are never recycled, so a caller keeps hitting the same warm
// sandbox. This is the configuration to compare against a plain long-lived
// sandbox per thread.
void BM_Zlib_SandboxPool_AlwaysReuse(benchmark::State& state) {
  RunZlibBenchmark(state, []() { return g_zlib_pool->Acquire(); });
}
BENCHMARK(BM_Zlib_SandboxPool_AlwaysReuse)
    ->Setup(ZlibPoolSetup<std::numeric_limits<size_t>::max()>)
    ->Teardown(ZlibPoolTeardown)
    ->UseRealTime()
    ->ThreadRange(1, 64);

}  // namespace

}  // namespace sapi
