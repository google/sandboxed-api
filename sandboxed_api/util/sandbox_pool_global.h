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

#ifndef SANDBOXED_API_UTIL_SANDBOX_POOL_GLOBAL_H_
#define SANDBOXED_API_UTIL_SANDBOX_POOL_GLOBAL_H_

#include <atomic>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/base/const_init.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "sandboxed_api/sandbox.h"
#include "sandboxed_api/util/sandbox_pool.h"

namespace sapi::state {

// Process-wide global registry for type-safe SandboxPool singletons.
template <typename SandboxT>
class GlobalSandboxPool {
 public:
  // Explicitly Init the pool with the given options and factory.
  static absl::Status Init(SandboxPoolOptions options,
                           typename SandboxPool<SandboxT>::Factory factory);

  // Overload for SAPI Sandbox subclasses.
  static absl::Status Init(SandboxPoolOptions options)
    requires std::is_base_of_v<sapi::SandboxBase, SandboxT>;

  // Acquires a sandbox from the process-wide global pool.
  // Lazily initializes the pool with default options on the first call
  // if not already initialized (only for SAPI Sandbox subclasses).
  static absl::StatusOr<SandboxHandle<SandboxT>> Acquire(
      absl::Duration timeout = absl::InfiniteDuration());

 private:
  static inline std::atomic<SandboxPool<SandboxT>*> pool_{nullptr};
  static inline std::shared_ptr<SandboxPool<SandboxT>> shared_pool_;
  static inline absl::Mutex mutex_{absl::kConstInit};
};

// Safe drop-in replacement for sapi::state::SandboxInstance<SandboxT>()
// Lazily initializes the underlying Global SandboxPool on the first call.
template <typename SandboxT>
absl::StatusOr<SandboxHandle<SandboxT>> AcquirePooledSandbox(
    absl::Duration timeout = absl::InfiniteDuration());

// --- Template Implementations ---

template <typename SandboxT>
absl::Status GlobalSandboxPool<SandboxT>::Init(
    SandboxPoolOptions options,
    typename SandboxPool<SandboxT>::Factory factory) {
  absl::MutexLock lock(mutex_);
  if (pool_.load(std::memory_order_acquire) != nullptr) {
    return absl::AlreadyExistsError(
        "Global SandboxPool already initialized for this type.");
  }
  ABSL_ASSIGN_OR_RETURN(auto pool, SandboxPool<SandboxT>::Create(
                                       std::move(options), std::move(factory)));

  shared_pool_ = pool;
  pool_.store(pool.get(), std::memory_order_release);
  return absl::OkStatus();
}

template <typename SandboxT>
absl::Status GlobalSandboxPool<SandboxT>::Init(SandboxPoolOptions options)
  requires std::is_base_of_v<sapi::SandboxBase, SandboxT>
{
  return Init(std::move(options),
              []() { return sapi::MakeSandbox<SandboxT>(); });
}

template <typename SandboxT>
absl::StatusOr<SandboxHandle<SandboxT>> GlobalSandboxPool<SandboxT>::Acquire(
    absl::Duration timeout) {
  auto* p = pool_.load(std::memory_order_acquire);
  if (p == nullptr) {
    absl::MutexLock lock(mutex_);
    p = pool_.load(std::memory_order_acquire);
    if (p == nullptr) {
      if constexpr (std::is_base_of_v<sapi::SandboxBase, SandboxT>) {
        ABSL_ASSIGN_OR_RETURN(
            auto pool, SandboxPool<SandboxT>::Create(SandboxPoolOptions()));
        shared_pool_ = pool;
        p = pool.get();
        pool_.store(p, std::memory_order_release);
      } else {
        return absl::FailedPreconditionError(
            "Global SandboxPool has not been initialized for this type.");
      }
    }
  }
  return p->Acquire(timeout);
}

template <typename SandboxT>
absl::StatusOr<SandboxHandle<SandboxT>> AcquirePooledSandbox(
    absl::Duration timeout) {
  return GlobalSandboxPool<SandboxT>::Acquire(timeout);
}

}  // namespace sapi::state

#endif  // SANDBOXED_API_UTIL_SANDBOX_POOL_GLOBAL_H_
