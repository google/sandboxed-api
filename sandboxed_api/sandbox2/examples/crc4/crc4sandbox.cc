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

// A demo sandbox for the crc4bin binary

#include <linux/filter.h>
#include <sys/resource.h>
#include <syscall.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
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
#include "sandboxed_api/sandbox2/ipc.h"
#include "sandboxed_api/sandbox2/limits.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/sandbox2/util/runfiles.h"

ABSL_FLAG(string, input, "", "Input to calculate CRC4 of.");
ABSL_FLAG(bool, call_syscall_not_allowed, false,
          "Have sandboxee call clone (violation).");

namespace {

std::unique_ptr<sandbox2::Policy> GetPolicy() {
  return sandbox2::PolicyBuilder()
      .DisableNamespaces()
      .AllowExit()
      .AddPolicyOnSyscalls(
          {__NR_read, __NR_write, __NR_close},
          {ARG_32(0), JEQ32(sandbox2::Comms::kSandbox2ClientCommsFD, ALLOW)})
#if defined(ADDRESS_SANITIZER) || defined(MEMORY_SANITIZER) || \
    defined(THREAD_SANITIZER)
      .AllowSyscall(__NR_mmap)
#endif
      .BuildOrDie();
}

bool SandboxedCRC4(sandbox2::Comms* comms, uint32_t* crc4) {
  std::string input(absl::GetFlag(FLAGS_input));

  const uint8_t* buf = reinterpret_cast<const uint8_t*>(input.data());
  size_t buf_size = input.size();

  if (!comms->SendBytes(buf, buf_size)) {
    LOG(ERROR) << "sandboxee_comms->SendBytes() failed";
    return false;
  }

  if (!comms->RecvUint32(crc4)) {
    LOG(ERROR) << "sandboxee_comms->RecvUint32(&crc4) failed";
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  if (absl::GetFlag(FLAGS_input).empty()) {
    LOG(ERROR) << "Parameter --input required.";
    return 1;
  }

  std::string path = sandbox2::GetInternalDataDependencyFilePath(
      "sandbox2/examples/crc4/crc4bin");
  std::vector<std::string> args = {path};
  if (absl::GetFlag(FLAGS_call_syscall_not_allowed)) {
    args.push_back("-call_syscall_not_allowed");
  }
  std::vector<std::string> envs = {};
  auto executor = absl::make_unique<sandbox2::Executor>(path, args, envs);

  executor
      // Sandboxing is enabled by the binary itself (i.e. the crc4bin is capable
      // of enabling sandboxing on its own).
      ->set_enable_sandbox_before_exec(false)
      .limits()
      // Remove restrictions on the size of address-space of sandboxed
      // processes.
      ->set_rlimit_as(RLIM64_INFINITY)
      // Kill sandboxed processes with a signal (SIGXFSZ) if it writes more than
      // these many bytes to the file-system.
      .set_rlimit_fsize(1024)
      .set_rlimit_cpu(60)  // The CPU time limit in seconds.
      .set_walltime_limit(absl::Seconds(5));

  auto* comms = executor->ipc()->comms();
  auto policy = GetPolicy();

  sandbox2::Sandbox2 s2(std::move(executor), std::move(policy));

  // Let the sandboxee run.
  if (!s2.RunAsync()) {
    auto result = s2.AwaitResult();
    LOG(ERROR) << "RunAsync failed: " << result.ToString();
    return 2;
  }

  uint32_t crc4;
  if (!SandboxedCRC4(comms, &crc4)) {
    LOG(ERROR) << "GetCRC4 failed";
    if (!s2.IsTerminated()) {
      // Kill the sandboxee, because failure to receive the data over the Comms
      // channel doesn't automatically mean that the sandboxee itself had
      // already finished. The final reason will not be overwritten, so if
      // sandboxee finished because of e.g. timeout, the TIMEOUT reason will be
      // reported.
      LOG(INFO) << "Killing sandboxee";
      s2.Kill();
    }
  }

  auto result = s2.AwaitResult();
  if (result.final_status() != sandbox2::Result::OK) {
    LOG(ERROR) << "Sandbox error: " << result.ToString();
    return 3;  // e.g. sandbox violation, signal (sigsegv)
  }
  auto code = result.reason_code();
  if (code) {
    LOG(ERROR) << "Sandboxee exited with non-zero: " << code;
    return 4;  // e.g. normal child error
  }
  LOG(INFO) << "Sandboxee finished: " << result.ToString();
  printf("0x%08x\n", crc4);
  return EXIT_SUCCESS;
}
