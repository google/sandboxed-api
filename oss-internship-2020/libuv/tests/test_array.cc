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

class UVTestArraySapiSandbox : public uv::UVSandbox {
 private:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    return sandbox2::PolicyBuilder()
        .AllowDynamicStartup()
        .AllowExit()
        .AllowFutexOp(FUTEX_WAKE_PRIVATE)
        .AllowOpen()
        .AllowSyscall(__NR_sysinfo)
        .AllowWrite()
        .BuildOrDie();
  }
};

class UVTestArray : public ::testing::Test {
 protected:
  void SetUp() override {
    sandbox_ = std::make_unique<UVTestArraySapiSandbox>();
    ASSERT_THAT(sandbox_->Init(), sapi::IsOk());
    api_ = std::make_unique<uv::UVApi>(sandbox_.get());
  }

  std::unique_ptr<UVTestArraySapiSandbox> sandbox_;
  std::unique_ptr<uv::UVApi> api_;
};

TEST_F(UVTestArray, LoadAvg) {
  double avg_buf[] = {-1, -1, -1};
  sapi::v::Array<double> avg(avg_buf, 3);

  // Check that loadavg is as initialized before call
  ASSERT_EQ(avg_buf[0], -1);
  ASSERT_EQ(avg_buf[1], -1);
  ASSERT_EQ(avg_buf[2], -1);

  // Get loadavg
  ASSERT_THAT(api_->sapi_uv_loadavg(avg.PtrBoth()), sapi::IsOk());

  // Check that loadavg values are positive
  ASSERT_GE(avg_buf[0], 0);
  ASSERT_GE(avg_buf[1], 0);
  ASSERT_GE(avg_buf[2], 0);
}

}  // namespace
