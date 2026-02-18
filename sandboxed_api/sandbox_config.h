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

#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "sandboxed_api/file_toc.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/synchronization/mutex.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/fork_client.h"
#include "sandboxed_api/sandbox2/limits.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/util/fileops.h"

namespace sapi {

// Context holding, potentially shared, fork client.
class ForkClientContext {
 public:
  explicit ForkClientContext(const FileToc* embed_lib_toc)
      : sandboxee_source_(embed_lib_toc) {
    CHECK(embed_lib_toc != nullptr);
  }
  // Path of the sandboxee:
  //  - relative to runfiles directory: ::sapi::GetDataDependencyFilePath()
  //    will be applied to it,
  //  - absolute: will be used as is.
  explicit ForkClientContext(std::string lib_path)
      : sandboxee_source_(std::move(lib_path)) {}

 private:
  friend class Sandbox2Backend;

  std::variant<const FileToc*, std::string> sandboxee_source_;
  struct SharedState {
    absl::Mutex mu_;
    std::shared_ptr<sandbox2::ForkClient> client_ ABSL_GUARDED_BY(mu_);
    std::shared_ptr<sandbox2::Executor> executor_ ABSL_GUARDED_BY(mu_);
  };
  std::shared_ptr<SharedState> shared_ = std::make_shared<SharedState>();
};

struct Sandbox2Config {
  // Optional. If not set, the default policy will be used.
  // See DefaultPolicyBuilder().
  std::unique_ptr<sandbox2::Policy> policy;

  // Includes the path to the sandboxee. Optional only if the generated embedded
  // sandboxee class is used.
  std::optional<ForkClientContext> fork_client_context;

  bool use_unotify_monitor = false;
  bool enable_log_server = false;
  bool enable_shared_memory = false;
  std::optional<std::string> cwd;
  std::optional<sandbox2::Limits> limits;

  // A generic policy which should work with majority of typical libraries,
  // which are single-threaded and require ~30 basic syscalls.
  static sandbox2::PolicyBuilder DefaultPolicyBuilder();

  static sandbox2::Limits DefaultLimits();
};

struct SandboxConfig {
  std::optional<std::vector<std::string>> environment_variables;
  std::optional<absl::flat_hash_map<std::string, std::string>>
      command_line_flags;
  // File descriptors to map into the sandbox.
  // The first element of the pair is the host fd, the second is the new fd in
  // the sandbox.
  std::optional<std::vector<std::pair<sapi::file_util::fileops::FDCloser, int>>>
      fd_mappings;

  Sandbox2Config sandbox2;

  static std::vector<std::string> DefaultEnvironmentVariables() {
    return {
    };
  }

  static absl::flat_hash_map<std::string, std::string> DefaultFlags() {
    return {
        {"stderrthreshold",
         std::to_string(static_cast<int>(absl::StderrThreshold()))},
    };
  }
};

}  // namespace sapi

#endif  // SANDBOXED_API_SANDBOX_CONFIG_H_
