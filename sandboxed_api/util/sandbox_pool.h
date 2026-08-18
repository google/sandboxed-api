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

#ifndef SANDBOXED_API_UTIL_SANDBOX_POOL_H_
#define SANDBOXED_API_UTIL_SANDBOX_POOL_H_

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "sandboxed_api/sandbox.h"
#include "sandboxed_api/util/sandbox_pool_internal.h"
#include "sandboxed_api/util/thread.h"

namespace sapi {

// Configuration options governing pool behavior.
struct SandboxPoolOptions {
  // The minimum number of sandboxes to keep in the pool. During initialization,
  // the pool will be pre-warmed to this number of sandboxes.
  size_t min_sandboxes = 1;
  // The maximum number of sandboxes to create. This is a hard limit, and
  // attempts to create more sandboxes will fail.
  size_t max_sandboxes = 1024;
  // The maximum amount of time a sandbox can remain idle in the pool before
  // being destroyed. Sandboxes that exceed this timeout will be destroyed.
  // Sandboxes will not be replaced upon destruction.
  absl::Duration idle_timeout = absl::Minutes(1);
  // The maximum number of times a sandbox can be used before it must be
  // recycled. A sandbox that has reached this limit will be destroyed and
  // replaced with a new one. `1` means that sandboxes are never reused.
  size_t max_sandbox_reuse = 1;
  // The number of threads to use for maintenance tasks, such as recycling and
  // destroying sandboxes. If 0, a default number of threads will be used,
  // based on the number of maximum sandboxes in the pool.
  size_t max_maintenance_threads = 0;
};

// Forward declarations
template <typename SandboxT>
class SandboxPool;

// Smart pointer RAII handle that automatically returns the sandbox to the pool
// when destroyed.
template <typename SandboxT>
class SandboxHandle {
 public:
  SandboxHandle() = default;
  SandboxHandle(
      std::unique_ptr<sandbox_pool_internal::PoolEntry<SandboxT>> entry,
      std::shared_ptr<SandboxPool<SandboxT>> pool)
      : entry_(std::move(entry)), pool_(std::move(pool)) {}

  ~SandboxHandle() {
    if (pool_ && entry_) {
      pool_->Release(std::move(entry_));
    }
  }

  // Move-only
  SandboxHandle(SandboxHandle&& other) noexcept
      : entry_(std::exchange(other.entry_, nullptr)),
        pool_(std::move(other.pool_)) {}

  SandboxHandle& operator=(SandboxHandle&& other) noexcept {
    if (this != &other) {
      if (pool_ && entry_) {
        pool_->Release(std::move(entry_));
      }
      entry_ = std::exchange(other.entry_, nullptr);
      pool_ = std::move(other.pool_);
    }
    return *this;
  }

  // Drop-in replacement smart pointer operations
  SandboxT* get() const { return entry_ ? entry_->sandbox.get() : nullptr; }
  SandboxT* operator->() const { return get(); }
  SandboxT& operator*() const { return *get(); }

  // Prevent copying
  SandboxHandle(const SandboxHandle&) = delete;
  SandboxHandle& operator=(const SandboxHandle&) = delete;

 private:
  std::unique_ptr<sandbox_pool_internal::PoolEntry<SandboxT>> entry_;
  std::shared_ptr<SandboxPool<SandboxT>> pool_;
};

// A thread-safe, scalable pool manager for SAPI and Sandbox2 sandboxes.
//
// This class is thread-safe and can be used to acquire and release sandboxes
// from multiple threads concurrently. The pool will automatically create new
// sandboxes as needed, up to the maximum number of sandboxes specified in the
// options. The pool will also automatically destroy and recycle sandboxes that
// are no longer needed, or that have exceeded the maximum usage limit.
//
// Example usage:
//
//   SandboxPoolOptions options;
//   options.min_sandboxes = 1;
//   options.max_sandboxes = 10;
//   options.max_sandbox_reuse = 2;
//   options.idle_timeout = absl::Seconds(10);
//   ABSL_ASSERT_OK_AND_ASSIGN(auto pool,
//                        SandboxPool<StringopSandbox>::Create(options));
//   ABSL_ASSERT_OK_AND_ASSIGN(auto handle1, pool->Acquire());
//   ABSL_ASSERT_OK_AND_ASSIGN(auto handle2, pool->Acquire());
template <typename SandboxT>
class SandboxPool : public std::enable_shared_from_this<SandboxPool<SandboxT>> {
 public:
  using Factory =
      absl::AnyInvocable<absl::StatusOr<std::unique_ptr<SandboxT>>()>;

  ~SandboxPool();

  // Creates a sandbox pool with the given options and factory.
  static absl::StatusOr<std::shared_ptr<SandboxPool>> Create(
      SandboxPoolOptions options, Factory factory);

  // Specialization for SAPI Sandbox subclasses.
  static absl::StatusOr<std::shared_ptr<SandboxPool>> Create(
      SandboxPoolOptions options)
    requires std::is_base_of_v<sapi::SandboxBase, SandboxT>;

  // Acquires a sandbox from the pool. Blocks up to the specified timeout if
  // the pool is exhausted.
  absl::StatusOr<SandboxHandle<SandboxT>> Acquire(
      absl::Duration timeout = absl::InfiniteDuration());

  // Available ready-to-use sandboxes in the pool.
  size_t AvailableCount() const { return idle_queue_.size(); }

 private:
  friend class SandboxHandle<SandboxT>;

  SandboxPool(SandboxPoolOptions options, Factory factory);

  absl::Status Init();
  void Release(
      std::unique_ptr<sandbox_pool_internal::PoolEntry<SandboxT>> entry);
  absl::StatusOr<std::unique_ptr<SandboxT>> CreateSandbox(size_t max_sandboxes);

  void WorkerRun();
  void Pruner();
  void CreateTask(size_t max_sandboxes);
  void DestroyTask(
      std::unique_ptr<sandbox_pool_internal::PoolEntry<SandboxT>> entry);
  void RecycleTask(
      std::unique_ptr<sandbox_pool_internal::PoolEntry<SandboxT>> entry);

  SandboxPoolOptions options_;
  Factory factory_;

  sandbox_pool_internal::ExpirableQueue<
      std::unique_ptr<sandbox_pool_internal::PoolEntry<SandboxT>>>
      idle_queue_;
  sandbox_pool_internal::Queue<absl::AnyInvocable<void() &&>> worker_queue_;

  std::atomic<size_t> active_count_ = 0;
  std::atomic<size_t> total_count_ = 0;

  sapi::Thread pruning_worker_;
  std::vector<sapi::Thread> maintenance_workers_;
};

// --- Template Implementations ---

template <typename SandboxT>
SandboxPool<SandboxT>::~SandboxPool() {
  idle_queue_.Stop();
  pruning_worker_.Join();
  worker_queue_.Stop();
  for (auto& worker : maintenance_workers_) {
    worker.Join();
  }

  CHECK_EQ(active_count_.load(std::memory_order_relaxed), 0);
}

template <typename SandboxT>
absl::StatusOr<std::shared_ptr<SandboxPool<SandboxT>>>
SandboxPool<SandboxT>::Create(SandboxPoolOptions options, Factory factory) {
  if (options.min_sandboxes > options.max_sandboxes) {
    return absl::InvalidArgumentError(
        "min_sandboxes must be less than or equal to max_sandboxes.");
  }

  if (options.max_sandboxes == 0) {
    return absl::InvalidArgumentError("max_sandboxes must be greater than 0.");
  }

  auto pool = std::shared_ptr<SandboxPool<SandboxT>>(
      new SandboxPool<SandboxT>(std::move(options), std::move(factory)));
  ABSL_RETURN_IF_ERROR(pool->Init());
  return pool;
}

template <typename SandboxT>
absl::StatusOr<std::shared_ptr<SandboxPool<SandboxT>>>
SandboxPool<SandboxT>::Create(SandboxPoolOptions options)
  requires std::is_base_of_v<sapi::SandboxBase, SandboxT>
{
  return Create(std::move(options),
                []() { return sapi::MakeSandbox<SandboxT>(); });
}

template <typename SandboxT>
absl::StatusOr<SandboxHandle<SandboxT>> SandboxPool<SandboxT>::Acquire(
    absl::Duration timeout) {
  // We are purposefully not checking whether the queues have stopped, since
  // this is a race condition with the destructor only.
  absl::Time deadline = absl::Now() + timeout;

  // First, try to grab an idle sandbox from the queue.
  std::unique_ptr<sandbox_pool_internal::PoolEntry<SandboxT>> entry;
  if (idle_queue_.TryPop(entry)) {
    active_count_.fetch_add(1, std::memory_order_relaxed);
    return SandboxHandle<SandboxT>(std::move(entry), this->shared_from_this());
  }

  // If there are no idle sandboxes, try creating a new one.
  auto sandbox = CreateSandbox(options_.max_sandboxes);
  if (sandbox.ok()) {
    active_count_.fetch_add(1, std::memory_order_relaxed);
    return SandboxHandle<SandboxT>(
        std::make_unique<sandbox_pool_internal::PoolEntry<SandboxT>>(
            std::move(*sandbox), 0),
        this->shared_from_this());
  }

  // If the pool is full, we'll need to fallback to waiting for an idle one.
  // Otherwise, return the error, as this means creating a new sandbox failed.
  if (!absl::IsResourceExhausted(sandbox.status())) {
    return sandbox.status();
  }

  // Wait for an idle sandbox.
  if (!idle_queue_.PopWithDeadline(entry, deadline)) {
    return absl::DeadlineExceededError("Sandbox pool acquisition timed out.");
  }
  active_count_.fetch_add(1, std::memory_order_relaxed);
  return SandboxHandle<SandboxT>(std::move(entry), this->shared_from_this());
}

template <typename SandboxT>
SandboxPool<SandboxT>::SandboxPool(SandboxPoolOptions options, Factory factory)
    : options_(std::move(options)), factory_(std::move(factory)) {}

template <typename SandboxT>
absl::Status SandboxPool<SandboxT>::Init() {
  pruning_worker_ =
      sapi::Thread(this, &SandboxPool<SandboxT>::Pruner, "sandbox_pool_pruner");
  size_t num_workers = options_.max_maintenance_threads;
  if (num_workers == 0) {
    num_workers = std::max(
        size_t{1}, static_cast<size_t>(std::log2(options_.max_sandboxes)));
  }
  maintenance_workers_.reserve(num_workers);
  for (size_t i = 0; i < num_workers; ++i) {
    maintenance_workers_.emplace_back(this, &SandboxPool<SandboxT>::WorkerRun,
                                      "sandbox_pool_maintenance");
  }
  // Pre-warm the pool to min_sandboxes. This will be done concurrently by the
  // maintenance workers, and those will ensure that no more than min_sandboxes
  // are created, so there is no need to wait for them to finish here.
  for (size_t i = 0; i < options_.min_sandboxes; ++i) {
    worker_queue_.Push(std::bind_front(&SandboxPool<SandboxT>::CreateTask, this,
                                       options_.min_sandboxes));
  }
  return absl::OkStatus();
}

template <typename SandboxT>
void SandboxPool<SandboxT>::Release(
    std::unique_ptr<sandbox_pool_internal::PoolEntry<SandboxT>> entry) {
  // Recycle if the usage count is at max usage.
  ++entry->usage_count;
  bool recycle = (entry->usage_count >= options_.max_sandbox_reuse);

  active_count_.fetch_sub(1, std::memory_order_relaxed);

  if (recycle) {
    // Needs recycling. Push to the worker queue to be recycled in a
    // maintenance thread.
    worker_queue_.Push(std::bind_front(&SandboxPool<SandboxT>::RecycleTask,
                                       this, std::move(entry)));
  } else {
    // Otherwise, push back to the idle queue immediately.
    idle_queue_.Push(std::move(entry));
  }
}

template <typename SandboxT>
void SandboxPool<SandboxT>::Pruner() {
  std::unique_ptr<sandbox_pool_internal::PoolEntry<SandboxT>> entry;
  while (idle_queue_.PopWhenExpired(entry, options_.idle_timeout,
                                    options_.min_sandboxes)) {
    worker_queue_.Push(std::bind_front(&SandboxPool<SandboxT>::DestroyTask,
                                       this, std::move(entry)));
  }
}

template <typename SandboxT>
absl::StatusOr<std::unique_ptr<SandboxT>> SandboxPool<SandboxT>::CreateSandbox(
    size_t max_sandboxes) {
  size_t total_count = total_count_.load(std::memory_order_relaxed);
  while (total_count < max_sandboxes) {
    if (total_count_.compare_exchange_weak(total_count, total_count + 1,
                                           std::memory_order_relaxed,
                                           std::memory_order_relaxed)) {
      auto sandbox = factory_();
      if (!sandbox.ok()) {
        total_count_.fetch_sub(1, std::memory_order_relaxed);
        return sandbox.status();
      }
      return sandbox;
    }
  }
  return absl::ResourceExhaustedError("Sandbox pool is full.");
}

template <typename SandboxT>
void SandboxPool<SandboxT>::CreateTask(size_t max_sandboxes) {
  auto sandbox = CreateSandbox(max_sandboxes);
  // If creating the sandbox failed but because of resource exhaustion, then
  // pool is full, and we should just terminate the task.
  if (!sandbox.ok()) {
    if (!absl::IsResourceExhausted(sandbox.status())) {
      LOG(ERROR) << "Failed to create sandbox: " << sandbox.status();
    }
    return;
  }
  idle_queue_.Push(std::make_unique<sandbox_pool_internal::PoolEntry<SandboxT>>(
      std::move(*sandbox), 0));
}

template <typename SandboxT>
void SandboxPool<SandboxT>::DestroyTask(
    std::unique_ptr<sandbox_pool_internal::PoolEntry<SandboxT>> entry) {
  total_count_.fetch_sub(1, std::memory_order_relaxed);
  // Explicitly destroy the sandbox here.
  entry.reset();
}

template <typename SandboxT>
void SandboxPool<SandboxT>::RecycleTask(
    std::unique_ptr<sandbox_pool_internal::PoolEntry<SandboxT>> entry) {
  // We do not call CreateSandbox() here, because we do not want this new
  // sandbox to be counted towards the max_sandboxes limit, since it will
  // replace a sandbox that is being destroyed.
  auto sandbox = factory_();
  if (!sandbox.ok()) {
    LOG(ERROR) << "Failed to create sandbox: " << sandbox.status();
    total_count_.fetch_sub(1, std::memory_order_relaxed);
    // Explicitly destroy the sandbox here.
    entry.reset();
    return;
  }
  idle_queue_.Push(std::make_unique<sandbox_pool_internal::PoolEntry<SandboxT>>(
      std::move(*sandbox), 0));
  // Explicitly destroy the old sandbox here.
  entry.reset();
}

template <typename SandboxT>
void SandboxPool<SandboxT>::WorkerRun() {
  absl::AnyInvocable<void() &&> task;
  while (worker_queue_.Pop(task)) {
    std::move(task)();
  }
}

}  // namespace sapi

#endif  // SANDBOXED_API_UTIL_SANDBOX_POOL_H_
