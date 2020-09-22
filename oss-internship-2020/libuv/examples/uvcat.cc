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

class UVSapiUVCatSandbox : public UVSandbox {
 public:
  UVSapiUVCatSandbox(std::string filename) : filename(filename) {}

 private:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    return sandbox2::PolicyBuilder()
        .AddFile(filename)
        .AllowDynamicStartup()
        .AllowExit()
        .AllowFork()
        .AllowFutexOp(FUTEX_WAKE_PRIVATE)
        .AllowFutexOp(FUTEX_WAIT_PRIVATE)
        .AllowMmap()
        .AllowOpen()
        .AllowSyscalls({__NR_epoll_create1, __NR_epoll_ctl, __NR_epoll_wait,
                        __NR_eventfd2, __NR_pipe2, __NR_prlimit64})
        .AllowWrite()
        .BuildOrDie();
  }

  std::string filename;
};

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  if (argc != 2) {
    LOG(FATAL) << "wrong number of arguments (1 expected)";
  }
  std::string filename_str = argv[1];

  absl::Status status;

  // Initialize sandbox2 and sapi
  UVSapiUVCatSandbox sandbox(filename_str);
  status = sandbox.Init();
  if (!status.ok()) {
    LOG(FATAL) << "Couldn't initialize Sandboxed API: " << status;
  }
  UVApi api(&sandbox);

  // Get remote pointer to the OnOpen method
  void* function_ptr;
  status = sandbox.rpc_channel()->Symbol("OnOpen", &function_ptr);
  if (!status.ok()) {
    LOG(FATAL) << "sapi::Sandbox::rpc_channel()->Symbol failed: " << status;
  }
  sapi::v::RemotePtr on_open(function_ptr);

  // Get remote pointer to the open_req variable
  void* open_req_voidptr;
  status = sandbox.rpc_channel()->Symbol("open_req", &open_req_voidptr);
  if (!status.ok()) {
    LOG(FATAL) << "sapi::Sandbox::rpc_channel()->Symbol failed: " << status;
  }
  sapi::v::RemotePtr open_req(open_req_voidptr);

  // Get default loop
  absl::StatusOr<void*> loop_voidptr = api.sapi_uv_default_loop();
  if (!loop_voidptr.ok()) {
    LOG(FATAL) << "sapi_uv_default_loop failed: " << loop_voidptr.status();
  }
  sapi::v::RemotePtr loop(loop_voidptr.value());

  absl::StatusOr<int> return_code;

  // Open file using the OnOpen callback (which will also read and print it)
  sapi::v::ConstCStr filename(filename_str.c_str());
  return_code = api.sapi_uv_fs_open(&loop, &open_req, filename.PtrBefore(),
                                    O_RDONLY, 0, &on_open);
  if (!return_code.ok()) {
    LOG(FATAL) << "sapi_uv_fs_open failed: " << return_code.status();
  }
  if (return_code.value() != 0) {
    LOG(FATAL) << "uv_fs_open returned error " << return_code.value();
  }

  // Run loop
  return_code = api.sapi_uv_run(&loop, UV_RUN_DEFAULT);
  if (!return_code.ok()) {
    LOG(FATAL) << "sapi_uv_run failed: " << return_code.status();
  }
  if (return_code.value() != 0) {
    LOG(FATAL) << "uv_run returned error " << return_code.value();
  }

  // Cleanup the request
  status = api.sapi_uv_fs_req_cleanup(&open_req);
  if (!status.ok()) {
    LOG(FATAL) << "sapi_uv_fs_req_cleanup failed: " << status;
  }

  return EXIT_SUCCESS;
}
