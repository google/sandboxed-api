// Copyright 2024 Google LLC
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

// A binary that tries to detect if it is running under sandbox2.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "absl/status/statusor.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/util.h"

namespace {

int TestSandboxSyscall() {
  absl::StatusOr<bool> is_running_under_sandbox2 =
      sandbox2::util::IsRunningInSandbox2();
  if (!is_running_under_sandbox2.ok()) {
    printf("Failed to check if running under sandbox2: %s\n",
           std::string(is_running_under_sandbox2.status().message()).c_str());
    return EXIT_FAILURE;
  }
  if (*is_running_under_sandbox2) {
    printf("Failed to correctly detect not running under sandbox2\n");
    return EXIT_FAILURE;
  }

  // Activate the sandbox and call the util::kMagicSyscallNo syscall again.
  auto comms = sandbox2::Comms(sandbox2::Comms::kSandbox2ClientCommsFD);
  sandbox2::Client client(&comms);
  client.SandboxMeHere();
  is_running_under_sandbox2 = sandbox2::util::IsRunningInSandbox2();
  if (!is_running_under_sandbox2.ok()) {
    printf("Failed to check if running under sandbox2: %s\n",
           std::string(is_running_under_sandbox2.status().message()).c_str());
    return EXIT_FAILURE;
  }
  if (!*is_running_under_sandbox2) {
    printf("Failed to correctly detect not running under sandbox2\n");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char* argv[]) {
  // Disable buffering.
  setbuf(stdin, nullptr);
  setbuf(stdout, nullptr);
  setbuf(stderr, nullptr);

  return TestSandboxSyscall();
}
