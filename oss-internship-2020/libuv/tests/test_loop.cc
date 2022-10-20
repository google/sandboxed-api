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

#include "gtest/gtest.h"
#include "absl/flags/flag.h"
#include "sandboxed_api/util/status_matchers.h"
#include "uv_sapi.sapi.h"  // NOLINT(build/include)

namespace {

class UVTestLoopSapiSandbox : public uv::UVSandbox {
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

class UVTestLoop : public ::testing::Test {
 protected:
  void SetUp() override {
    sandbox_ = std::make_unique<UVTestLoopSapiSandbox>();
    ASSERT_THAT(sandbox_->Init(), sapi::IsOk());
    api_ = std::make_unique<uv::UVApi>(sandbox_.get());
  }

  // Check sapi_uv_loop_init
  void UVLoopInit(sapi::v::Ptr* loop) {
    SAPI_ASSERT_OK_AND_ASSIGN(int error_code, api_->sapi_uv_loop_init(loop));
    ASSERT_EQ(error_code, 0);
  }

  // Check sapi_uv_run
  void UVRun(sapi::v::Ptr* loop) {
    SAPI_ASSERT_OK_AND_ASSIGN(int error_code,
                              api_->sapi_uv_run(loop, UV_RUN_DEFAULT));
    ASSERT_EQ(error_code, 0);
  }

  // Check sapi_uv_loop_close
  void UVLoopClose(sapi::v::Ptr* loop) {
    SAPI_ASSERT_OK_AND_ASSIGN(int error_code, api_->sapi_uv_loop_close(loop));
    ASSERT_EQ(error_code, 0);
  }

  // Check sapi_uv_default_loop, set loop to default loop
  void UVDefaultLoop(sapi::v::Ptr* loop) {
    SAPI_ASSERT_OK_AND_ASSIGN(void* loop_voidptr, api_->sapi_uv_default_loop());
    loop->SetRemote(loop_voidptr);
  }

  std::unique_ptr<UVTestLoopSapiSandbox> sandbox_;
  std::unique_ptr<uv::UVApi> api_;
};

TEST_F(UVTestLoop, InitLoop) {
  // Allocate memory for loop
  void* loop_voidptr;
  ASSERT_THAT(
      sandbox_->rpc_channel()->Allocate(sizeof(uv_loop_t), &loop_voidptr),
      sapi::IsOk());
  sapi::v::RemotePtr loop(loop_voidptr);

  // Initialize, run and close the manually initialized loop
  UVLoopInit(loop.PtrBoth());
  UVRun(loop.PtrNone());
  UVLoopClose(loop.PtrNone());

  // Free loop memory
  ASSERT_THAT(sandbox_->rpc_channel()->Free(loop_voidptr), sapi::IsOk());
}

TEST_F(UVTestLoop, DefaultLoop) {
  sapi::v::RemotePtr loop(nullptr);

  // Run the default loop
  UVDefaultLoop(&loop);
  UVRun(loop.PtrNone());

  // Close the default loop
  UVDefaultLoop(&loop);
  UVLoopClose(loop.PtrNone());
}

}  // namespace
