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

#ifndef SANDBOXED_API_SANDBOX_CONFIG_H_
#define SANDBOXED_API_SANDBOX_CONFIG_H_

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/linked_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/die_if_null.h"
#include "absl/synchronization/mutex.h"
#include "sandboxed_api/embed_toc.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/fork_client.h"
#include "sandboxed_api/sandbox2/limits.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/sandbox_config.h"

namespace sapi {

// Context holding, potentially shared, fork client.
class ForkClientContext {
 public:
  explicit ForkClientContext(sapi::EmbedToc embed_lib_toc)
      : sandboxee_source_(embed_lib_toc) {
    CHECK(!embed_lib_toc.name.empty());
  }

  explicit ForkClientContext(const sapi::EmbedToc* embed_lib_toc)
      : ForkClientContext(*ABSL_DIE_IF_NULL(embed_lib_toc)) {}

  // Path of the sandboxee:
  //  - relative to runfiles directory: ::sapi::GetDataDependencyFilePath()
  //    will be applied to it,
  //  - absolute: will be used as is.
  explicit ForkClientContext(std::string lib_path)
      : sandboxee_source_(std::move(lib_path)) {}

 private:
  friend class Sandbox2Backend;

  std::variant<sapi::EmbedToc, std::string> sandboxee_source_;
  struct SharedState {
    absl::Mutex mu_;
    std::shared_ptr<sandbox2::ForkClient> client_ ABSL_GUARDED_BY(mu_);
    std::shared_ptr<sandbox2::Executor> executor_ ABSL_GUARDED_BY(mu_);
  };
  std::shared_ptr<SharedState> shared_ = std::make_shared<SharedState>();
};

struct Sandbox2Config {
  // By default, we use a small 128KiB shared memory region to avoid potential
  // performance degradation when transferring large objects to the sandboxee.
  static constexpr size_t kDefaultSAPISharedMemorySize = (128ULL << 10);

  // Returns a PolicyBuilder pre-configured with a baseline policy that works
  // with the majority of typical single-threaded libraries.
  // This is the preferred way of building a sandbox policy in most cases
  // because it simplifies maintenance as runtime and tooling requirements
  // evolve, while still allowing callers to chain additional rules onto the
  // returned builder.
  //
  // This policy is used by `SandboxConfig::DefaultConfig()` (and when `policy`
  // is unset) and is designed to work together with the defaults in
  // `SandboxConfig` (such as `SandboxConfig::DefaultFlags()` and
  // `SandboxConfig::DefaultEnvironmentVariables()`).
  static sandbox2::PolicyBuilder DefaultPolicyBuilder();

  // Returns the default resource and time limits for SAPI sandboxees (disabling
  // CPU and wall-time limits by default). Used by
  // `SandboxConfig::DefaultConfig()` and when `limits` is unset.
  static sandbox2::Limits DefaultLimits();

  // Optional. If not set, the default policy will be used.
  // See DefaultPolicyBuilder().
  std::unique_ptr<sandbox2::Policy> policy;

  // Includes the path to the sandboxee. Optional only if the generated embedded
  // sandboxee class is used.
  std::optional<ForkClientContext> fork_client_context;

  // Configuration for the shared memory region. If set to nullopt, the
  // sandboxee will not map a shared memory region (default).
  std::optional<sandbox2::SharedMemoryConfig> shared_memory_config;

  bool use_unotify_monitor = false;
  bool enable_log_server = false;
  bool enable_shared_memory_comms = false;
  // If true, we will spawn a new Thread in the sandboxee for each thread in the
  // host that makes a call to the sandbox.
  // WARNING: Reusing the same sandbox instance (sequentially or concurrently)
  // has security implications. A successful sandboxee exploit might allow
  // attacker to read/write data from/to other threads using the same sandbox
  // instance.
  bool enable_multithreading = false;
  std::optional<std::string> cwd;
  // Optional. If not set, `DefaultLimits()` will be used.
  std::optional<sandbox2::Limits> limits;
};

// Configuration for a SAPI sandbox.
//
// By default, generated SAPI sandbox classes initialize using
// `SandboxConfig::DefaultConfig()`, which combines
// `Sandbox2Config::DefaultPolicyBuilder()`, `Sandbox2Config::DefaultLimits()`,
// shared memory communication, `DefaultFlags()`, and
// `DefaultEnvironmentVariables()`.
struct SandboxConfig {
  // Returns the default environment variables passed to the sandboxee when
  // `environment_variables` is unset. When customizing environment variables,
  // prefer starting with `DefaultEnvironmentVariables()` and appending to it.
  static std::vector<std::string> DefaultEnvironmentVariables() {
    return {
    };
  }

  // Returns the default command-line flags passed to the sandboxee when
  // `command_line_flags` is unset. These configure baseline logging and
  // runtime behavior for the sandboxee. When customizing flags, prefer
  // starting with `DefaultFlags()` and modifying the returned map.
  static absl::linked_hash_map<std::string, std::string> DefaultFlags();

  // Returns the default `SandboxConfig` used by generated SAPI sandbox
  // classes. It combines the default policy, limits, flags, and environment
  // variables.
  //
  // Prefer starting with `SandboxConfig::DefaultConfig()` and
  // modifying the returned struct rather than default-constructing
  // `SandboxConfig{}`.
  static SandboxConfig DefaultConfig();

  SandboxConfig& set_name(std::string n) {
    name = std::move(n);
    return *this;
  }

  std::string name = "unknown";

  // Optional. If not set, `DefaultEnvironmentVariables()` will be used.
  std::optional<std::vector<std::string>> environment_variables;

  // Optional. If not set, `DefaultFlags()` will be used.
  // Use a linked hash map to preserve the order of flags. This is relevant for
  // flags like --undefok, where order matters.
  std::optional<absl::linked_hash_map<std::string, std::string>>
      command_line_flags;

  // File descriptors to map into the sandbox.
  // The first element of the pair is the host fd, the second is the new fd in
  // the sandbox.
  std::optional<std::vector<std::pair<sapi::file_util::fileops::FDCloser, int>>>
      fd_mappings;

  Sandbox2Config sandbox2;
};

}  // namespace sapi

#endif  // SANDBOXED_API_SANDBOX_CONFIG_H_
