// Copyright 2019 Google LLC
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

// A demo sandbox for the static_bin binary.
// Use: static_sandbox --logtostderr

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <syscall.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/limits.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/runfiles.h"

std::unique_ptr<sandbox2::Policy> GetPolicy() {
  return sandbox2::PolicyBuilder()
      // The most frequent syscall should go first in this sequence (to make it
      // fast).
      // Allow read() with all arguments.
      .AllowRead()
      // Allow a preset of syscalls that are known to be used during startup
      // of static binaries.
      .AllowStaticStartup()
      // Allow the getpid() syscall.
      .AllowSyscall(__NR_getpid)

      // Examples for AddPolicyOnSyscall:
      .AddPolicyOnSyscall(__NR_write,
                          {
                              // Load the first argument of write() (= fd)
                              ARG_32(0),
                              // Allow write(fd=STDOUT)
                              JEQ32(1, ALLOW),
                              // Allow write(fd=STDERR)
                              JEQ32(2, ALLOW),
                              // Fall-through for every other case.
                              // The default action will be KILL if it is not
                              // explicitly ALLOWed by a following rule.
                          })
      // write() calls with fd not in (1, 2) will continue evaluating the
      // policy. This means that other rules might still allow them.

      // Allow the Sandboxee to set the name for better recognition in the
      // process listing.
      .AllowPrctlSetName()

      // Allow the dynamic loader to mark pages to never allow read-write-exec.
      .AddPolicyOnSyscall(__NR_mprotect,
                          {
                              ARG_32(2),
                              JEQ32(PROT_READ, ALLOW),
                              JEQ32(PROT_NONE, ALLOW),
                              JEQ32(PROT_READ | PROT_WRITE, ALLOW),
                              JEQ32(PROT_READ | PROT_EXEC, ALLOW),
                          })

      // Allow exit() only with an exit_code of 0.
      // Explicitly jumping to KILL, thus the following rules can not
      // override this rule.
      .AddPolicyOnSyscall(
          __NR_exit_group,
          {
              // Load first argument (exit_code).
              ARG_32(0),
              // Deny every argument except 0.
              JNE32(0, KILL),
              // Allow all exit() calls that were not previously forbidden
              // = exit_code == 0.
              ALLOW,
          })

      // = This won't have any effect as we handled every case of this syscall
      // in the previous rule.
      .AllowSyscall(__NR_exit_group)

      .BlockSyscallsWithErrno(
          {
#ifdef __NR_access
              // On Debian, even static binaries check existence of
              // /etc/ld.so.nohwcap.
              __NR_access,
#endif

#ifdef __NR_open
              __NR_open,
#endif
              __NR_openat,
          },
          ENOENT)
      .BuildOrDie();
}

int main(int argc, char* argv[]) {
  // This test is incompatible with sanitizers.
  // The `SKIP_SANITIZERS_AND_COVERAGE` macro won't work for us here since we
  // need to return something.
  if constexpr (sapi::sanitizers::IsAny()) {
    return EXIT_SUCCESS;
  }
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  // Note: In your own code, use sapi::GetDataDependencyFilePath() instead.
  const std::string path = sapi::internal::GetSapiDataDependencyFilePath(
      "sandbox2/examples/static/static_bin");
  std::vector<std::string> args = {path};
  auto executor = std::make_unique<sandbox2::Executor>(path, args);

  executor
      // Sandboxing is enabled by the sandbox itself. The sandboxed binary is
      // not aware that it'll be sandboxed.
      // Note: 'true' is the default setting for this class.
      ->set_enable_sandbox_before_exec(true)
      .limits()
      // Remove restrictions on the size of address-space of sandboxed
      // processes.
      ->set_rlimit_as(RLIM64_INFINITY)
      // Kill sandboxed processes with a signal (SIGXFSZ) if it writes more than
      // these many bytes to the file-system.
      .set_rlimit_fsize(1024 * 1024)
      // The CPU time limit.
      .set_rlimit_cpu(60)
      .set_walltime_limit(absl::Seconds(30));

  int proc_version_fd = open("/proc/version", O_RDONLY);
  PCHECK(proc_version_fd != -1);

  // Map this fils to sandboxee's stdin.
  executor->ipc()->MapFd(proc_version_fd, STDIN_FILENO);

  auto policy = GetPolicy();
  sandbox2::Sandbox2 s2(std::move(executor), std::move(policy));

  // Let the sandboxee run (synchronously).
  sandbox2::Result result = s2.Run();

  LOG(INFO) << "Final execution status: " << result.ToString();

  return result.final_status() == sandbox2::Result::OK ? EXIT_SUCCESS
                                                       : EXIT_FAILURE;
}
