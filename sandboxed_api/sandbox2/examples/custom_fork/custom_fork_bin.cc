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

// This file is an example of a binary which is intended to be sandboxed by the
// sandbox2, and which uses a built-in fork-server to spawn new sandboxees
// (instead of doing fork/execve via the Fork-Server).

#include <sys/types.h>

#include <cstdint>

#include "absl/base/log_severity.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/forkingclient.h"
#include "sandboxed_api/util/raw_logging.h"

// Just return the value received over the Comms channel from the parent.
static int SandboxeeFunction(sandbox2::Comms* comms) {
  int32_t i;
  // SAPI_RAW_CHECK() uses smaller set of syscalls than regular CHECK().
  SAPI_RAW_CHECK(comms->RecvInt32(&i), "Receiving an int32_t");

  // Make sure that we're not the init process in the custom forkserver
  // child.
  SAPI_RAW_CHECK(getpid() == 2, "Unexpected PID");
  return i;
}

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);

  // Writing to stderr limits the number of invoked syscalls.
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::InitializeLog();

  // Instantiate Comms channel with the parent Executor
  sandbox2::Comms comms(sandbox2::Comms::kDefaultConnection);
  sandbox2::ForkingClient s2client(&comms);

  for (;;) {
    // Start a new process, if the sandboxer requests us to do so. No need to
    // wait for the new process, as the call to sandbox2::Client::Fork will
    // indirectly call sigaction(SIGCHLD, sa_flags=SA_NOCLDWAIT) in the parent.
    pid_t pid = s2client.WaitAndFork();
    if (pid == -1) {
      SAPI_RAW_CHECK(false, "Could not spawn a new sandboxee");
    }
    // Child - return to the main(), to continue with code which is supposed to
    // be sandboxed. From now on the comms channel (in the child) is set up over
    // a new file descriptor pair, reachable from a separate Executor in the
    // sandboxer.
    if (pid == 0) {
      break;
    }
  }

  // Start sandboxing here
  s2client.SandboxMeHere();

  // This section of code runs sandboxed
  return SandboxeeFunction(&comms);
}
