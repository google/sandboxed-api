// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
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

#include "sandboxed_api/util/flag.h"
#include "uv_sapi.sapi.h"

class UVSapiHelloworldSandbox : public UVSandbox {
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

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  absl::Status status;

  // Initialize sandbox2 and sapi
  UVSapiHelloworldSandbox sandbox;
  status = sandbox.Init();
  if (!status.ok()) {
    LOG(FATAL) << "Couldn't initialize Sandboxed API: " << status;
  }
  UVApi api(&sandbox);

  // Allocate memory for the uv_loop_t object
  void* loop_voidptr;
  status = sandbox.rpc_channel()->Allocate(sizeof(uv_loop_t), &loop_voidptr);
  if (!status.ok()) {
    LOG(FATAL) << "sapi::Sandbox::rpc_channel()->Allocate failed: " << status;
  }
  sapi::v::RemotePtr loop(loop_voidptr);

  absl::StatusOr<int> return_code;

  // Initialize loop
  return_code = api.sapi_uv_loop_init(&loop);
  if (!return_code.ok()) {
    LOG(FATAL) << "sapi_uv_loop_init failed: " << return_code.status();
  }
  if (return_code.value() != 0) {
    LOG(FATAL) << "uv_loop_init returned error " << return_code.value();
  }

  std::cout << "The loop is about to quit" << std::endl;

  // Run loop
  return_code = api.sapi_uv_run(&loop, UV_RUN_DEFAULT);
  if (!return_code.ok()) {
    LOG(FATAL) << "sapi_uv_run failed: " << return_code.status();
  }
  if (return_code.value() != 0) {
    LOG(FATAL) << "uv_run returned error " << return_code.value();
  }

  // Close loop
  return_code = api.sapi_uv_loop_close(&loop);
  if (!return_code.ok()) {
    LOG(FATAL) << "sapi_uv_loop_close failed: " << return_code.status();
  }
  if (return_code.value() != 0) {
    LOG(FATAL) << "uv_loop_close returned error " << return_code.value();
  }

  return EXIT_SUCCESS;
}
