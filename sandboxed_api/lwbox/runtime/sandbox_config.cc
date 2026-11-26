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

#include "sandboxed_api/lwbox/runtime/sandbox_config.h"

#include <sys/resource.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/time/time.h"
#include "sandboxed_api/embed_toc.h"
#include "sandboxed_api/sandbox.h"
#include "sandboxed_api/sandbox2/allowlists/seccomp_speculation.h"
#include "sandboxed_api/sandbox2/limits.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/sandbox_config.h"
#include "sandboxed_api/sandbox_config.h"
#include "sandboxed_api/util/checked_math.h"

// uint16_t bounds the region to less than 64 GiB, so that parsing the flag
// rejects larger (and negative) values instead of the sandbox failing on the
// first call.
ABSL_FLAG(uint16_t, lwbox_shared_memory_mb, 128,
          "Size in MiB (at most 65535) of the memory region that the Sandbox2 "
          "sandbox of lightweight sandboxed libraries shares with the host to "
          "pass buffers without extra copies. Buffers that don't fit are "
          "allocated in the sandboxee and copied through the sandbox's RPC "
          "channel instead, which is slower. Pages are allocated on first use "
          "and stay allocated while the sandbox runs. 0 disables the region. "
          "Has no effect with other sandbox backends. Must be set before the "
          "first call into a sandboxed library.");

namespace sapi::lwbox {

sandbox2::PolicyBuilder DefaultPolicyBuilder() {
  sandbox2::PolicyBuilder builder =
      sapi::Sandbox2Config::DefaultPolicyBuilder();
  builder.AllowMultithreading();
  builder.Allow(sandbox2::SeccompSpeculation());
  return builder;
}

::sapi::SandboxConfig CreateMergedSandboxConfig(
    std::unique_ptr<sandbox2::Policy> policy,
    const ::sapi::EmbedToc* embed_toc) {
  std::optional<sandbox2::SharedMemoryConfig> shared_memory_config;
  if (const uint16_t size_mb = absl::GetFlag(FLAGS_lwbox_shared_memory_mb);
      size_mb > 0) {
    shared_memory_config = sandbox2::SharedMemoryConfig{
        .size = size_t{size_mb} << 20,
        .enable_huge_pages = true,
    };
  }
  return {
      .name = "SapiMergedSandbox",
      .environment_variables =
          ::sapi::SandboxConfig::DefaultEnvironmentVariables(),
      .command_line_flags = ::sapi::SandboxConfig::DefaultFlags(),
      .sandbox2 =
          {
              .policy = std::move(policy),
              .fork_client_context = ::sapi::ForkClientContext(embed_toc),
              .shared_memory_config = shared_memory_config,
              .enable_shared_memory_comms = true,
              .enable_multithreading = true,
              // We're permissive with limits by default to not break libraries
              // that might use a lot of memory/cpu.
              .limits = sandbox2::Limits()
                            .set_rlimit_as(RLIM64_INFINITY)
                            .set_rlimit_cpu(RLIM64_INFINITY)
                            .set_rlimit_fsize(RLIM64_INFINITY)
                            .set_rlimit_nofile(RLIM64_INFINITY)
                            .set_rlimit_core(RLIM64_INFINITY)
                            .set_walltime_limit(absl::ZeroDuration()),
          },
  };
}

MergedSandbox::MergedSandbox(std::unique_ptr<::sapi::SandboxBase> sandbox)
    : ::sapi::SandboxBase(sandbox->name()), sandbox_(std::move(sandbox)) {
  Check(Init());
}

void MergedSandbox::Check(const absl::Status& status) {
  CHECK_OK(status) << "SAPI merged sandbox failed";
}

size_t MergedSandbox::CheckedMultiply(size_t x, size_t y) {
  return ::sapi::CheckedMultiply(x, y);
}

}  // namespace sapi::lwbox
