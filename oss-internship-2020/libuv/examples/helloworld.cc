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

#include <linux/futex.h>
#include <syscall.h>
#include <uv.h>

#include <iostream>

#include "absl/flags/flag.h"
#include "uv_sapi.sapi.h"  // NOLINT(build/include)

namespace {

class UVSapiHelloworldSandbox : public uv::UVSandbox {
 private:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    return sandbox2::PolicyBuilder()
        .AllowDynamicStartup()
        .AllowExit()
        .AllowFutexOp(FUTEX_WAKE_PRIVATE)
        .AllowSyscalls({__NR_epoll_create1, __NR_eventfd2, __NR_pipe2})
        .AllowWrite()
        .BuildOrDie();
  }
};

absl::Status HelloWorld() {
  // Initialize sandbox2 and sapi
  UVSapiHelloworldSandbox sandbox;
  SAPI_RETURN_IF_ERROR(sandbox.Init());
  uv::UVApi api(&sandbox);

  // Allocate memory for the uv_loop_t object
  void* loop_voidptr;
  SAPI_RETURN_IF_ERROR(
      sandbox.rpc_channel()->Allocate(sizeof(uv_loop_t), &loop_voidptr));
  sapi::v::RemotePtr loop(loop_voidptr);

  int return_code;

  // Initialize loop
  SAPI_ASSIGN_OR_RETURN(return_code, api.sapi_uv_loop_init(&loop));
  if (return_code != 0) {
    return absl::UnavailableError("uv_loop_init returned error " + return_code);
  }

  std::cout << "The loop is about to quit" << std::endl;

  // Run loop
  SAPI_ASSIGN_OR_RETURN(return_code, api.sapi_uv_run(&loop, UV_RUN_DEFAULT));
  if (return_code != 0) {
    return absl::UnavailableError("uv_run returned error " + return_code);
  }

  // Close loop
  SAPI_ASSIGN_OR_RETURN(return_code, api.sapi_uv_loop_close(&loop));
  if (return_code != 0) {
    return absl::UnavailableError("uv_loop_close returned error " +
                                  return_code);
  }

  return absl::OkStatus();
}

}  // namespace

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  sapi::InitLogging(argv[0]);

  if (absl::Status status = HelloWorld(); !status.ok()) {
    LOG(ERROR) << "HelloWorld failed: " << status.ToString();
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
