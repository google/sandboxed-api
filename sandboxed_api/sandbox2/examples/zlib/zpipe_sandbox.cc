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

#include <fcntl.h>
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

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/limits.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/runfiles.h"

ABSL_FLAG(std::string, input, "", "Input file");
ABSL_FLAG(std::string, output, "", "Output file");
ABSL_FLAG(bool, decompress, false, "Decompress instead of compress.");

namespace {

std::unique_ptr<sandbox2::Policy> GetPolicy() {
  return sandbox2::PolicyBuilder()
      // Allow read on STDIN.
      .AddPolicyOnSyscall(__NR_read, {ARG_32(0), JEQ32(0, ALLOW)})
      // Allow write on STDOUT / STDERR.
      .AddPolicyOnSyscall(__NR_write,
                          {ARG_32(0), JEQ32(1, ALLOW), JEQ32(2, ALLOW)})
      .AllowStat()
      .AllowStaticStartup()
      .AllowSystemMalloc()
      .AllowExit()
      .BlockSyscallsWithErrno(
          {
#ifdef __NR_access
              __NR_access,
#endif
              __NR_faccessat,
          },
          ENOENT)
      .BuildOrDie();
}

}  // namespace

int main(int argc, char* argv[]) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  if (absl::GetFlag(FLAGS_input).empty()) {
    LOG(ERROR) << "Parameter --input required.";
    return 1;
  }

  if (absl::GetFlag(FLAGS_output).empty()) {
    LOG(ERROR) << "Parameter --output required.";
    return 1;
  }

  // Note: In your own code, use sapi::GetDataDependencyFilePath() instead.
  const std::string path = sapi::internal::GetSapiDataDependencyFilePath(
      "sandbox2/examples/zlib/zpipe");
  std::vector<std::string> args = {path};
  if (absl::GetFlag(FLAGS_decompress)) {
    args.push_back("-d");
  }
  std::vector<std::string> envs = {};
  auto executor = std::make_unique<sandbox2::Executor>(path, args, envs);

  executor
      // Kill sandboxed processes with a signal (SIGXFSZ) if it writes more than
      // these many bytes to the file-system.
      ->limits()
      ->set_rlimit_fsize(1ULL << 30)  // 1GiB
      .set_rlimit_cpu(60)             // The CPU time limit in seconds.
      .set_walltime_limit(absl::Seconds(5));

  // Create input + output FD.
  int fd_in = open(absl::GetFlag(FLAGS_input).c_str(), O_RDONLY);
  int fd_out = open(absl::GetFlag(FLAGS_output).c_str(),
                    O_WRONLY | O_CREAT | O_TRUNC, 0644);
  CHECK_GE(fd_in, 0);
  CHECK_GE(fd_out, 0);
  executor->ipc()->MapFd(fd_in, STDIN_FILENO);
  executor->ipc()->MapFd(fd_out, STDOUT_FILENO);

  auto policy = GetPolicy();
  sandbox2::Sandbox2 s2(std::move(executor), std::move(policy));

  // Let the sandboxee run.
  auto result = s2.Run();
  close(fd_in);
  close(fd_out);
  if (result.final_status() != sandbox2::Result::OK) {
    LOG(ERROR) << "Sandbox error: " << result.ToString();
    return 2;  // e.g. sandbox violation, signal (sigsegv)
  }
  auto code = result.reason_code();
  if (code) {
    LOG(ERROR) << "Sandboxee exited with non-zero: " << code;
    return 3;  // e.g. normal child error
  }
  LOG(INFO) << "Sandboxee finished: " << result.ToString();
  return EXIT_SUCCESS;
}
