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
#include "absl/log/initialize.h"
#include "uv_sapi.sapi.h"  // NOLINT(build/include)

namespace {

class UVSapiIdleBasicSandbox : public uv::UVSandbox {
 private:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    return sandbox2::PolicyBuilder()
        .AllowDynamicStartup()
        .AllowExit()
        .AllowFutexOp(FUTEX_WAKE_PRIVATE)
        .AllowSyscalls({__NR_epoll_create1, __NR_epoll_ctl, __NR_epoll_wait,
                        __NR_eventfd2, __NR_pipe2})
        .AllowWrite()
        .BuildOrDie();
  }
};

absl::Status IdleBasic() {
  // Initialize sandbox2 and sapi
  UVSapiIdleBasicSandbox sandbox;
  SAPI_RETURN_IF_ERROR(sandbox.Init());
  uv::UVApi api(&sandbox);

  // Get remote pointer to the IdleCallback method
  void* function_ptr;
  SAPI_RETURN_IF_ERROR(
      sandbox.rpc_channel()->Symbol("IdleCallback", &function_ptr));
  sapi::v::RemotePtr idle_callback(function_ptr);

  // Allocate memory for the uv_idle_t object
  void* idle_voidptr;
  SAPI_RETURN_IF_ERROR(
      sandbox.rpc_channel()->Allocate(sizeof(uv_idle_t), &idle_voidptr));
  sapi::v::RemotePtr idler(idle_voidptr);

  int return_code;

  // Get default loop
  SAPI_ASSIGN_OR_RETURN(void* loop_voidptr, api.sapi_uv_default_loop());
  sapi::v::RemotePtr loop(loop_voidptr);

  // Initialize idler
  SAPI_ASSIGN_OR_RETURN(return_code, api.sapi_uv_idle_init(&loop, &idler));
  if (return_code != 0) {
    return absl::UnavailableError("sapi_uv_idle_init returned error " +
                                  return_code);
  }

  // Start idler
  SAPI_ASSIGN_OR_RETURN(return_code,
                        api.sapi_uv_idle_start(&idler, &idle_callback));
  if (return_code != 0) {
    return absl::UnavailableError("sapi_uv_idle_start returned error " +
                                  return_code);
  }

  // Run loop
  SAPI_ASSIGN_OR_RETURN(return_code, api.sapi_uv_run(&loop, UV_RUN_DEFAULT));
  if (return_code != 0) {
    return absl::UnavailableError("uv_run returned error " + return_code);
  }

  // Close idler
  sapi::v::NullPtr null_ptr;
  SAPI_RETURN_IF_ERROR(api.sapi_uv_close(&idler, &null_ptr));

  // Close loop
  SAPI_ASSIGN_OR_RETURN(return_code, api.sapi_uv_loop_close(&loop));
  // UV_EBUSY is accepted because it is the return code of uv_loop_close
  // in the original example
  if (return_code != 0 && return_code != UV_EBUSY) {
    return absl::UnavailableError("uv_loop_close returned error " +
                                  return_code);
  }

  return absl::OkStatus();
}

}  // namespace

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  if (absl::Status status = IdleBasic(); !status.ok()) {
    LOG(ERROR) << "IdleBasic failed: " << status.ToString();
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
