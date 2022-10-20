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
#include <uv.h>

#include "gtest/gtest.h"
#include "absl/flags/flag.h"
#include "sandboxed_api/util/status_matchers.h"
#include "uv_sapi.sapi.h"  // NOLINT(build/include)

namespace {

class UVTestErrorSapiSandbox : public uv::UVSandbox {
 private:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    return sandbox2::PolicyBuilder()
        .AllowDynamicStartup()
        .AllowExit()
        .AllowFutexOp(FUTEX_WAKE_PRIVATE)
        .AllowWrite()
        .BuildOrDie();
  }
};

class UVTestError : public ::testing::Test {
 protected:
  void SetUp() override {
    sandbox_ = std::make_unique<UVTestErrorSapiSandbox>();
    ASSERT_THAT(sandbox_->Init(), sapi::IsOk());
    api_ = std::make_unique<uv::UVApi>(sandbox_.get());
  }

  // Check sapi_uv_strerror on error
  void UVStrerror(int error) {
    // Call sapi_uv_strerror
    absl::StatusOr<void*> error_message_ptr = api_->sapi_uv_strerror(error);
    ASSERT_THAT(error_message_ptr, sapi::IsOk());

    // Get error message from the sandboxee
    SAPI_ASSERT_OK_AND_ASSIGN(
        std::string error_message,
        sandbox_->GetCString(sapi::v::RemotePtr{error_message_ptr.value()}));

    // Check that it is equal to expected error message
    ASSERT_EQ(error_message, std::string{uv_strerror(error)});
  }

  // Check sapi_uv_translate_sys_error on error
  void UVTranslateSysError(int error) {
    // Call sapi_uv_translate_sys_error and get error code
    SAPI_ASSERT_OK_AND_ASSIGN(int error_code,
                              api_->sapi_uv_translate_sys_error(error));

    // Check that it is equal to expected error code
    ASSERT_EQ(error_code, uv_translate_sys_error(error));
  }

  std::unique_ptr<UVTestErrorSapiSandbox> sandbox_;
  std::unique_ptr<uv::UVApi> api_;
};

TEST_F(UVTestError, ErrorMessage) {
  // Test sapi_uv_strerror method
  UVStrerror(0);
  UVStrerror(UV_EINVAL);
  UVStrerror(1337);
  UVStrerror(-1337);
}

TEST_F(UVTestError, SystemError) {
  // Test sapi_uv_translate_sys_error method
  UVTranslateSysError(EPERM);
  UVTranslateSysError(EPIPE);
  UVTranslateSysError(EINVAL);
  UVTranslateSysError(UV_EINVAL);
  UVTranslateSysError(UV_ERANGE);
  UVTranslateSysError(UV_EACCES);
  UVTranslateSysError(0);
}

}  // namespace
