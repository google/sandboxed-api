// Copyright 2025 Google LLC
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

#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/global_forkclient.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/testing.h"

ABSL_FLAG(int, comms_fd, -1, "Fd to use for comms");
ABSL_FLAG(bool, unotify_monitor, false, "Use unotify monitor");

using ::sapi::CreateDefaultPermissiveTestPolicy;
using ::sapi::GetTestSourcePath;

sandbox2::PolicyBuilder CreateDefaultTestPolicy(absl::string_view path) {
  return CreateDefaultPermissiveTestPolicy(path).CollectStacktracesOnSignal(
      false);
}

int main(int argc, char* argv[]) {
  PCHECK(setpgid(0, 0) == 0);
  // Child process
  pid_t pid = fork();
  if (pid == 0) {
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
    absl::ParseCommandLine(argc, argv);
    absl::InitializeLog();
    sandbox2::GlobalForkClient::EnsureStarted();
    sandbox2::Comms comms(absl::GetFlag(FLAGS_comms_fd));
    const std::string path = GetTestSourcePath(
        "sandbox2/testcases/terminate_process_group_sandboxee");
    std::vector<std::string> args = {path};
    sandbox2::Sandbox2 sandbox(std::make_unique<sandbox2::Executor>(path, args),
                               CreateDefaultTestPolicy(path).BuildOrDie());
    if (absl::GetFlag(FLAGS_unotify_monitor)) {
      CHECK_OK(sandbox.EnableUnotifyMonitor());
    }
    CHECK(sandbox.RunAsync());
    // Move to a new process group
    PCHECK(setpgid(0, 0) == 0);
    bool unused;
    // Wait for sandboxee to start fully
    CHECK(sandbox.comms()->RecvBool(&unused));
    // Communicate sandboxee was started
    CHECK(comms.SendBool(true));
    // Wait for notification that the parent was killed
    CHECK(comms.RecvBool(&unused));
    // Communicate to the sandboxee it can exit
    CHECK(sandbox.comms()->SendBool(true));
    sandbox2::Result result = sandbox.AwaitResult();
    CHECK_EQ(result.final_status(), sandbox2::Result::OK);
    CHECK_EQ(result.reason_code(), 0);
    // Communicate sandboxee exited
    CHECK(comms.SendBool(true));
    _exit(0);
  }
  int status;
  PCHECK(waitpid(pid, &status, 0) == 0);
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  _exit(1);
}
