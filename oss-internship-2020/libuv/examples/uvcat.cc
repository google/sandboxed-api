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

class UVSapiUVCatSandbox : public uv::UVSandbox {
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

absl::Status UVCat(std::string filearg) {
  // Initialize sandbox2 and sapi
  UVSapiUVCatSandbox sandbox(filearg);
  SAPI_RETURN_IF_ERROR(sandbox.Init());
  uv::UVApi api(&sandbox);

  // Get remote pointer to the OnOpen method
  void* function_ptr;
  SAPI_RETURN_IF_ERROR(sandbox.rpc_channel()->Symbol("OnOpen", &function_ptr));
  sapi::v::RemotePtr on_open(function_ptr);

  // Get remote pointer to the open_req variable
  void* open_req_voidptr;
  SAPI_RETURN_IF_ERROR(
      sandbox.rpc_channel()->Symbol("open_req", &open_req_voidptr));
  sapi::v::RemotePtr open_req(open_req_voidptr);

  // Get default loop
  SAPI_ASSIGN_OR_RETURN(void* loop_voidptr, api.sapi_uv_default_loop());
  sapi::v::RemotePtr loop(loop_voidptr);

  int return_code;

  // Open file using the OnOpen callback (which will also read and print it)
  sapi::v::ConstCStr filename(filearg.c_str());
  SAPI_ASSIGN_OR_RETURN(
      return_code, api.sapi_uv_fs_open(&loop, &open_req, filename.PtrBefore(),
                                       O_RDONLY, 0, &on_open));
  if (return_code != 0) {
    return absl::UnavailableError("uv_fs_open returned error " + return_code);
  }

  // Run loop
  SAPI_ASSIGN_OR_RETURN(return_code, api.sapi_uv_run(&loop, UV_RUN_DEFAULT));
  if (return_code != 0) {
    return absl::UnavailableError("uv_run returned error " + return_code);
  }

  // Cleanup the request
  SAPI_RETURN_IF_ERROR(api.sapi_uv_fs_req_cleanup(&open_req));

  return absl::OkStatus();
}

}  // namespace

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  sapi::InitLogging(argv[0]);

  if (argc != 2) {
    LOG(ERROR) << "wrong number of arguments (1 expected)";
    return EXIT_FAILURE;
  }

  if (absl::Status status = UVCat(argv[1]); !status.ok()) {
    LOG(ERROR) << "UVCat failed: " << status.ToString();
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
