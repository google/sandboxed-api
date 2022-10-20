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
#include <sys/socket.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>

#include "absl/log/globals.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/forkserver.h"
#include "sandboxed_api/sandbox2/sanitizer.h"
#include "sandboxed_api/util/raw_logging.h"

int main() {
  // Make sure the logs go stderr.
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  // Close all non-essential FDs to keep newly opened FD numbers consistent.
  absl::Status status = sandbox2::sanitizer::CloseAllFDsExcept(
      {0, 1, 2, sandbox2::Comms::kSandbox2ClientCommsFD});

  if (!status.ok()) {
    SAPI_RAW_LOG(WARNING, "Closing non-essential FDs failed");
  }

  // Make the process' name easily recognizable with ps/pstree.
  if (prctl(PR_SET_NAME, "S2-FORK-SERV", 0, 0, 0) != 0) {
    SAPI_RAW_PLOG(WARNING, "prctl(PR_SET_NAME, 'S2-FORK-SERV')");
  }

  // Don't react (with stack-tracing) to SIGTERM's sent from other processes
  // (e.g. from the borglet or SubProcess). This ForkServer should go down if
  // the parent goes down (or if the GlobalForkServerComms is closed), which is
  // assured by prctl(PR_SET_PDEATHSIG, SIGKILL) being called in the
  // ForkServer::Initialize(). We don't want to change behavior of non-global
  // ForkServers, hence it's called here and not in the
  // ForkServer::Initialize().
  struct sigaction sa;
  sa.sa_handler = SIG_IGN;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGTERM, &sa, nullptr) == -1) {
    SAPI_RAW_PLOG(WARNING, "sigaction(SIGTERM, sa_handler=SIG_IGN)");
  }

  sandbox2::Comms comms(sandbox2::Comms::kDefaultConnection);
  sandbox2::ForkServer fork_server(&comms);

  while (true) {
    pid_t child_pid = fork_server.ServeRequest();
    if (!child_pid) {
      // FORKSERVER_FORK sent to the global forkserver. This case does not make
      // sense, we thus kill the process here.
      _Exit(0);
    }
  }
}
