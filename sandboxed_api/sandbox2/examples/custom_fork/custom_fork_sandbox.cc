// Copyright 2020 Google LLC. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// A demo sandbox for the custom_fork_bin binary.
// Use: custom_fork_sandbox --logtostderr

#include <syscall.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glog/logging.h>
#include "sandboxed_api/util/flag.h"
#include "absl/memory/memory.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/forkserver.h"
#include "sandboxed_api/sandbox2/ipc.h"
#include "sandboxed_api/sandbox2/limits.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2/util/runfiles.h"

std::unique_ptr<sandbox2::Policy> GetPolicy() {
  return sandbox2::PolicyBuilder()
      // The most frequent syscall should go first in this sequence (to make it
      // fast).
      .AllowRead()
      .AllowWrite()
      .AllowExit()
      .AllowTime()
      .AllowSyscalls({
        __NR_close, __NR_getpid,
#if defined(__NR_arch_prctl)
            // Not defined with every CPU architecture in Prod.
            __NR_arch_prctl,
#endif  // defined(__NR_arch_prctl)
      })
#if defined(ADDRESS_SANITIZER) || defined(MEMORY_SANITIZER) || \
    defined(THREAD_SANITIZER)
      .AllowMmap()
#endif
      .BuildOrDie();
}

static int SandboxIteration(sandbox2::ForkClient* fork_client, int32_t i) {
  // Now, start the sandboxee as usual, just use a different Executor
  // constructor, which takes pointer to the ForkClient.
  auto executor = absl::make_unique<sandbox2::Executor>(fork_client);

  // Set limits as usual.
  executor
      ->limits()
      // Remove restrictions on the size of address-space of sandboxed
      // processes. Here, it's 1GiB.
      ->set_rlimit_as(
#if defined(ADDRESS_SANITIZER) || defined(MEMORY_SANITIZER) || \
    defined(THREAD_SANITIZER)
          RLIM64_INFINITY
#else
          1ULL << 30  // 1GiB
#endif
          )
      // Kill sandboxed processes with a signal (SIGXFSZ) if it writes more than
      // these many bytes to the file-system (including logs in prod, which
      // write to files STDOUT and STDERR).
      .set_rlimit_fsize(1024 /* bytes */)
      // The CPU time limit.
      .set_rlimit_cpu(10 /* CPU-seconds */)
      .set_walltime_limit(absl::Seconds(5));

  auto* comms = executor->ipc()->comms();

  auto policy = GetPolicy();
  sandbox2::Sandbox2 s2(std::move(executor), std::move(policy));

  // Let the sandboxee run (asynchronously).
  CHECK(s2.RunAsync());
  // Send integer, which will be returned as the sandboxee's exit code.
  CHECK(comms->SendInt32(i));
  auto result = s2.AwaitResult();

  LOG(INFO) << "Final execution status of PID " << s2.GetPid() << ": "
            << result.ToString();

  if (result.final_status() != sandbox2::Result::OK) {
    return -1;
  }
  return result.reason_code();
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  // This test is incompatible with sanitizers.
  // The `SKIP_SANITIZERS_AND_COVERAGE` macro won't work for us here since we
  // need to return something.
#if defined(ADDRESS_SANITIZER) || defined(MEMORY_SANITIZER) || \
    defined(THREAD_SANITIZER)
  return EXIT_SUCCESS;
#endif

  // Start a custom fork-server (via sandbox2::Executor).
  const std::string path = sandbox2::GetInternalDataDependencyFilePath(
      "sandbox2/examples/custom_fork/custom_fork_bin");
  std::vector<std::string> args = {path};
  std::vector<std::string> envs = {};
  auto fork_executor = absl::make_unique<sandbox2::Executor>(path, args, envs);
  // Start the fork-server (which is here: the custom_fork_bin process calling
  // sandbox2::Client::WaitAndFork() in a loop).
  //
  // This function returns immediately, returning std::unique_ptr<ForkClient>.
  //
  // If it's holding the nullptr, then this call had failed.
  auto fork_client = fork_executor->StartForkServer();
  if (!fork_client) {
    LOG(ERROR) << "Starting custom ForkServer failed";
    return EXIT_FAILURE;
  }
  LOG(INFO) << "Custom Fork-Server started";

  // Test new sandboxees: send them integers over Comms, and expect they will
  // exit with these specific exit codes.
  for (int i = 0; i < 10; i++) {
    CHECK_EQ(SandboxIteration(fork_client.get(), i), i);
  }

  return EXIT_SUCCESS;
}
