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

class UVTestCallbackSapiSandbox : public uv::UVSandbox {
 private:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    return sandbox2::PolicyBuilder()
        .DangerDefaultAllowAll()
        .AllowDynamicStartup()
        .AllowExit()
        .AllowFutexOp(FUTEX_WAKE_PRIVATE)
        .AllowSyscalls({__NR_epoll_create1, __NR_eventfd2, __NR_pipe2})
        .AllowWrite()
        .BuildOrDie();
  }
};

class UVTestCallback : public ::testing::Test {
 protected:
  void SetUp() override {
    sandbox_ = std::make_unique<UVTestCallbackSapiSandbox>();
    ASSERT_THAT(sandbox_->Init(), sapi::IsOk());
    api_ = std::make_unique<uv::UVApi>(sandbox_.get());
  }

  // Check sapi_uv_timer_init
  void UVTimerInit(sapi::v::Ptr* loop, sapi::v::Ptr* timer) {
    SAPI_ASSERT_OK_AND_ASSIGN(int error_code,
                              api_->sapi_uv_timer_init(loop, timer));
    ASSERT_EQ(error_code, 0);
  }

  // Check sapi_uv_timer_start
  // (actual time is ignored because timeout and repeat are 0)
  void UVTimerStart(sapi::v::Ptr* timer) {
    // Get the TimerCallback callback from the sandbox
    void* timer_cb_voidptr;
    ASSERT_THAT(
        sandbox_->rpc_channel()->Symbol("TimerCallback", &timer_cb_voidptr),
        sapi::IsOk());
    sapi::v::RemotePtr timer_cb(timer_cb_voidptr);

    // Set the timer's callback, timeout and repeat
    SAPI_ASSERT_OK_AND_ASSIGN(
        int error_code, api_->sapi_uv_timer_start(timer, &timer_cb, 0, 0));
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

  std::unique_ptr<UVTestCallbackSapiSandbox> sandbox_;
  std::unique_ptr<uv::UVApi> api_;

  static constexpr int kData = 1729;
};

TEST_F(UVTestCallback, TimerCallback) {
  // Initialize loop
  sapi::v::RemotePtr loop(nullptr);

  // Allocate memory for timer
  void* timer_voidptr;
  ASSERT_THAT(
      sandbox_->rpc_channel()->Allocate(sizeof(uv_timer_t), &timer_voidptr),
      sapi::IsOk());
  sapi::v::RemotePtr timer(timer_voidptr);

  // Initialize timer and add it to default loop
  UVDefaultLoop(&loop);
  UVTimerInit(loop.PtrNone(), timer.PtrBoth());

  // Set timer data to kData
  sapi::v::Int data(kData);
  void* data_voidptr;
  ASSERT_THAT(sandbox_->rpc_channel()->Allocate(sizeof(int), &data_voidptr),
              sapi::IsOk());
  data.SetRemote(data_voidptr);
  ASSERT_THAT(api_->sapi_uv_handle_set_data(timer.PtrBoth(), data.PtrBefore()),
              sapi::IsOk());

  // Start the timer
  UVTimerStart(timer.PtrBoth());

  // Check that data has not changed (because the loop is not running yet)
  // This is done by resetting the local value and then getting the remote one
  data.SetValue(0);
  ASSERT_THAT(sandbox_->TransferFromSandboxee(&data), sapi::IsOk());
  ASSERT_EQ(data.GetValue(), kData);

  // Run the loop
  UVDefaultLoop(&loop);
  UVRun(loop.PtrNone());

  // Check that data has changed (and therefore callback was called correctly)
  ASSERT_THAT(sandbox_->TransferFromSandboxee(&data), sapi::IsOk());
  ASSERT_EQ(data.GetValue(), kData + 1);

  // Close the loop
  UVDefaultLoop(&loop);
  UVLoopClose(loop.PtrNone());
}

}  // namespace
