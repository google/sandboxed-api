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

#include "absl/flags/flag.h"
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

ABSL_FLAG(bool, connect_with_handler, true, "Connect using automatic mode.");

namespace {

constexpr char kSandboxeePath[] =
    "sandbox2/examples/network_proxy/networkproxy_bin";

std::unique_ptr<sandbox2::Policy> GetPolicy(absl::string_view sandboxee_path) {
  sandbox2::PolicyBuilder builder;
  builder.AllowExit()
      .AllowMmap()
      .AllowRead()
      .AllowWrite()
      .AllowStat()  // printf, puts
      .AllowOpen()
      .AllowSyscall(__NR_sendto)  // send
      .AllowSyscall(__NR_lseek)
      .AllowSyscall(__NR_munmap)
      .AllowSyscall(__NR_getpid)
      .AllowTcMalloc()
      .AddLibrariesForBinary(sandboxee_path);
  if (absl::GetFlag(FLAGS_connect_with_handler)) {
    builder.AddNetworkProxyHandlerPolicy();
  } else {
    builder.AddNetworkProxyPolicy();
  }
  return builder.AllowIPv6("::1").BuildOrDie();
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
  const std::string path =
      sapi::internal::GetSapiDataDependencyFilePath(kSandboxeePath);
  std::vector<std::string> args = {path};
  if (!absl::GetFlag(FLAGS_connect_with_handler)) {
    args.push_back("--noconnect_with_handler");
  }
  std::vector<std::string> envs = {};

  auto executor = std::make_unique<sandbox2::Executor>(path, args, envs);

  executor
      // Sandboxing is enabled by the binary itself (i.e. the networkproxy_bin
      // is capable of enabling sandboxing on its own).
      ->set_enable_sandbox_before_exec(false)
      // Set cwd to / to get rids of warnings connected with file namespace.
      .set_cwd("/");
  executor
      ->limits()
      // Kill sandboxed processes with a signal (SIGXFSZ) if it writes more than
      // these many bytes to the file-system.
      ->set_rlimit_fsize(10000)
      // The CPU time limit in seconds.
      .set_rlimit_cpu(100)
      .set_walltime_limit(absl::Seconds(100));

  auto policy = GetPolicy(path);
  sandbox2::Sandbox2 s2(std::move(executor), std::move(policy));
  sandbox2::Comms* comms = s2.comms();

  // Let the sandboxee run.
  if (!s2.RunAsync()) {
    auto result = s2.AwaitResult();
    LOG(ERROR) << "RunAsync failed: " << result.ToString();
    return 2;
  }

  // Send the port number via comms
  if (!comms->SendInt32((*server)->port())) {
    LOG(ERROR) << "sandboxee_comms->SendInt32() failed";
    return 3;
  }

  auto result = s2.AwaitResult();
  if (result.final_status() != sandbox2::Result::OK) {
    LOG(ERROR) << "Sandbox error: " << result.ToString();
    return 4;  // e.g. sandbox violation, signal (sigsegv)
  }
  auto code = result.reason_code();
  if (code) {
    LOG(ERROR) << "Sandboxee exited with non-zero: " << code;
    return 5;  // e.g. normal child error
  }

  LOG(INFO) << "Sandboxee finished: " << result.ToString();
  return EXIT_SUCCESS;
}
