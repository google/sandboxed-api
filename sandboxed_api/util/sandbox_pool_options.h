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

#ifndef SANDBOXED_API_UTIL_SANDBOX_POOL_OPTIONS_H_
#define SANDBOXED_API_UTIL_SANDBOX_POOL_OPTIONS_H_

#include <cstddef>
#include <limits>
#include <optional>

#include "absl/time/time.h"

namespace sapi {

// Configuration options governing pool behavior.
struct SandboxPoolOptions {
  // The minimum number of sandboxes to keep in the pool. During initialization,
  // the pool will be pre-warmed to this number of sandboxes.
  size_t min_sandboxes = 1;
  // The maximum number of sandboxes to create. Defaults to unbounded. If set to
  // a finite limit, attempts to acquire more sandboxes than this limit will
  // block until one is released or the acquisition timeout expires.
  size_t max_sandboxes = std::numeric_limits<size_t>::max();
  // The maximum amount of time a sandbox can remain idle in the pool before
  // being destroyed. Sandboxes that exceed this timeout will be destroyed.
  // Sandboxes will not be replaced upon destruction.
  absl::Duration idle_timeout = absl::Minutes(2);
  // The maximum number of times a sandbox can be used before it must be
  // recycled. A sandbox that has reached this limit will be destroyed and
  // replaced with a new one. `1` means that sandboxes are never reused.
  size_t max_sandbox_reuse = 1;
  // The number of threads to use for maintenance tasks, such as recycling and
  // destroying sandboxes.
  // - std::nullopt (default): A default number of threads will be used,
  //   based on the number of maximum sandboxes in the pool.
  // - 0: Threadless mode. No maintenance or pruning background threads are
  //   spawned; sandboxes are created on demand and destroyed synchronously.
  // - N > 0: Spawns N maintenance threads plus 1 pruning thread.
  std::optional<size_t> max_maintenance_threads = std::nullopt;
};

}  // namespace sapi

#endif  // SANDBOXED_API_UTIL_SANDBOX_POOL_OPTIONS_H_
