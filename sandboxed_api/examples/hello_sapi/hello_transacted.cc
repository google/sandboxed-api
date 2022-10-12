// Copyright 2020 Google LLC
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

// These are needed for the __NR_xxx syscall numbers
#include <linux/audit.h>
#include <sys/syscall.h>

#include <iostream>
#include <memory>

#include "absl/memory/memory.h"
#include "absl/status/status.h"

// Generated header
#include "hello_sapi.sapi.h"  // NOLINT(build/include)
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/transaction.h"
#include "sandboxed_api/util/status_macros.h"

namespace {

class CustomHelloSandbox : public HelloSandbox {
 public:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    // Return a new policy.
    return sandbox2::PolicyBuilder()
        .AllowRead()
        .AllowWrite()
        .AllowOpen()
        .AllowSystemMalloc()
        .AllowHandleSignals()
        .AllowExit()
        .AllowStat()
        .AllowTime()
        .AllowGetIDs()
        .AllowGetPIDs()
        .AllowSyscalls({
            __NR_tgkill,
            __NR_recvmsg,
            __NR_sendmsg,
            __NR_lseek,
            __NR_nanosleep,
            __NR_futex,
            __NR_close,
        })
        .AddFile("/etc/localtime")
        .BuildOrDie();
  }
};

}  // namespace

int main() {
  std::cout << "Calling into a sandboxee to add two numbers...\n";

  sapi::BasicTransaction transaction(std::make_unique<CustomHelloSandbox>());

  absl::Status status =
      transaction.Run([](sapi::Sandbox* sandbox) -> absl::Status {
        HelloApi api(sandbox);
        SAPI_ASSIGN_OR_RETURN(int result, api.AddTwoIntegers(1000, 337));
        std::cout << "  1000 + 337 = " << result << "\n";
        return absl::OkStatus();
      });
  if (!status.ok()) {
    std::cerr << "Error during sandbox call: " << status.message() << "\n";
  }
}
