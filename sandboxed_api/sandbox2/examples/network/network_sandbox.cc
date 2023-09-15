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

// A demo sandbox for the network binary.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <syscall.h>
#include <unistd.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/base/log_severity.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/network_proxy/testing.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/util/runfiles.h"

namespace {

std::unique_ptr<sandbox2::Policy> GetPolicy(absl::string_view sandboxee_path) {
  return sandbox2::PolicyBuilder()
      .AllowExit()
      .AllowMmap()
      .AllowRead()
      .AllowWrite()
      .AllowSyscall(__NR_close)
      .AllowSyscall(__NR_recvmsg)  // RecvFD
      .AllowSyscall(__NR_sendto)   // send
      .AllowStat()                 // printf,puts
      .AddLibrariesForBinary(sandboxee_path)
      .AllowTcMalloc()
      .BuildOrDie();
}

int ConnectToServer(int port) {
  int s = socket(AF_INET6, SOCK_STREAM, 0);
  if (s < 0) {
    PLOG(ERROR) << "socket() failed";
    return -1;
  }

  struct sockaddr_in6 saddr {};
  saddr.sin6_family = AF_INET6;
  saddr.sin6_port = htons(port);

  int err = inet_pton(AF_INET6, "::1", &saddr.sin6_addr);
  if (err == 0) {
    LOG(ERROR) << "inet_pton() failed";
    close(s);
    return -1;
  }
  if (err == -1) {
    PLOG(ERROR) << "inet_pton() failed";
    close(s);
    return -1;
  }

  err = connect(s, reinterpret_cast<const struct sockaddr*>(&saddr),
                sizeof(saddr));
  if (err != 0) {
    LOG(ERROR) << "connect() failed";
    close(s);
    return -1;
  }

  LOG(INFO) << "Connected to the server";
  return s;
}

bool HandleSandboxee(sandbox2::Comms* comms, int port) {
  // Connect to the server and pass the file descriptor to sandboxee.
  int client = ConnectToServer(port);
  if (client <= 0) {
    LOG(ERROR) << "connect_to_server() failed";
    return false;
  }

  if (!comms->SendFD(client)) {
    LOG(ERROR) << "sandboxee_comms->SendFD(client) failed";
    close(client);
    return false;
  }
  close(client);
  return true;
}

}  // namespace

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

  absl::StatusOr<std::unique_ptr<sandbox2::NetworkProxyTestServer>> server =
      sandbox2::NetworkProxyTestServer::Start();
  if (!server.ok()) {
    LOG(ERROR) << server.status();
    return EXIT_FAILURE;
  }

  // Note: In your own code, use sapi::GetDataDependencyFilePath() instead.
  const std::string path = sapi::internal::GetSapiDataDependencyFilePath(
      "sandbox2/examples/network/network_bin");
  std::vector<std::string> args = {path};
  std::vector<std::string> envs = {};

  auto executor = std::make_unique<sandbox2::Executor>(path, args, envs);
  executor
      // Sandboxing is enabled by the binary itself (i.e. the crc4bin is capable
      // of enabling sandboxing on its own).
      ->set_enable_sandbox_before_exec(false)
      // Set cwd to / to get rids of warnings connected with file namespace.
      .set_cwd("/");

  executor
      ->limits()
      // Kill sandboxed processes with a signal (SIGXFSZ) if it writes more than
      // these many bytes to the file-system.
      ->set_rlimit_fsize(10000)
      .set_rlimit_cpu(100)  // The CPU time limit in seconds
      .set_walltime_limit(absl::Seconds(100));

  auto policy = GetPolicy(path);
  sandbox2::Sandbox2 s2(std::move(executor), std::move(policy));
  auto* comms = s2.comms();

  // Let the sandboxee run.
  if (!s2.RunAsync()) {
    auto result = s2.AwaitResult();
    LOG(ERROR) << "RunAsync failed: " << result.ToString();
    return 2;
  }

  if (!HandleSandboxee(comms, (*server)->port())) {
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
    return 3;  // e.g. sandbox violation, signal (sigsegv).
  }
  auto code = result.reason_code();
  if (code) {
    LOG(ERROR) << "Sandboxee exited with non-zero: " << code;
    return 4;  // e.g. normal child error.
  }
  LOG(INFO) << "Sandboxee finished: " << result.ToString();
  return EXIT_SUCCESS;
}
