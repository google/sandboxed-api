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

#include <sys/prctl.h>

#include <csignal>
#include <cstdlib>

#include "absl/base/log_severity.h"
#include "absl/log/globals.h"
#include "absl/status/status.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/forkserver.h"
#include "sandboxed_api/sandbox2/sanitizer.h"
#include "sandboxed_api/sandbox2/unwind/unwind.h"
#include "sandboxed_api/util/raw_logging.h"

namespace sandbox2 {
namespace {

int ForkserverMain() {
  SAPI_RAW_PCHECK(setpgid(0, 0) == 0, "setpgid(0, 0) failed");

  // Make sure the logs go stderr. We won't initialize the logging library.
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  // Close all non-essential FDs to keep newly opened FD numbers consistent.
  sanitizer::CloseAllFDsExcept({0, 1, 2, Comms::kSandbox2ClientCommsFD});

  // Make the process' name easily recognizable with ps/pstree.
  if (prctl(PR_SET_NAME, "S2-FORK-SERV", 0, 0, 0) != 0) {
    SAPI_RAW_PLOG(WARNING, "prctl(PR_SET_NAME, 'S2-FORK-SERV')");
  }

  // Don't react (with stack-tracing) to SIGTERM's sent from other processes
  // (e.g. from the borglet or SubProcess). This ForkServer should go down if
  // the parent goes down (or if the GlobalForkServerComms is closed), which is
  // assured by prctl(PR_SET_PDEATHSIG, SIGKILL) being called in the
  // ForkServer::Initialize(). We don't want to change behavior of non-global
  // ForkServers, hence it's called here and not in ForkServer::Initialize().
  {
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGTERM, &sa, nullptr) == -1) {
      SAPI_RAW_PLOG(WARNING, "sigaction(SIGTERM, sa_handler=SIG_IGN)");
    }
  }

  Comms comms(Comms::kDefaultConnection);
  ForkServer fork_server(&comms);
  sanitizer::WaitForSanitizer();

  while (!fork_server.IsTerminated()) {
    if (fork_server.ServeRequest() != 0) {
      // Non-child process or error. Errors are logged internally.
      continue;
    }

    Client client(&comms);
    client.SandboxMeHere();

    if (absl::Status status = RunLibUnwindAndSymbolizer(&comms); !status.ok()) {
      SAPI_RAW_LOG(ERROR, "RunLibUnwindAndSymbolizer failed: %.*s",
                   static_cast<int>(status.message().size()),
                   status.message().data());
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }
  SAPI_RAW_VLOG(1, "ForkServer Comms closed. Exiting");
  return EXIT_SUCCESS;
}

}  // namespace
}  // namespace sandbox2

int main() { return sandbox2::ForkserverMain(); }
