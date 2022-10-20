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

class UVTestOSSapiSandbox : public uv::UVSandbox {
 private:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    return sandbox2::PolicyBuilder()
        .AllowDynamicStartup()
        .AllowExit()
        .AllowFutexOp(FUTEX_WAKE_PRIVATE)
        .AllowGetIDs()
        .AllowMmap()
        .AllowOpen()
        .AllowWrite()
        .AllowSyscalls({__NR_connect, __NR_socket})
        .DisableNamespaces()
        .BuildOrDie();
  }
};

class UVTestOS : public ::testing::Test {
 protected:
  void SetUp() override {
    sandbox_ = std::make_unique<UVTestOSSapiSandbox>();
    ASSERT_THAT(sandbox_->Init(), sapi::IsOk());
    api_ = std::make_unique<uv::UVApi>(sandbox_.get());
  }

  std::unique_ptr<UVTestOSSapiSandbox> sandbox_;
  std::unique_ptr<uv::UVApi> api_;

  static constexpr size_t kBigBufLen = 4096;
  static constexpr size_t kSmallBufLen = 1;
};

TEST_F(UVTestOS, HomeDirBig) {
  // Get expected home directory
  char expected_homedir[kBigBufLen];
  size_t len = sizeof(expected_homedir);
  ASSERT_GE(uv_os_homedir(expected_homedir, &len), 0);

  // Get home directory from the sandbox
  sapi::v::Array<char> uv_homedir(kBigBufLen);
  uv_homedir[0] = '\0';
  sapi::v::IntBase<size_t> uv_homedir_len(kBigBufLen);
  SAPI_ASSERT_OK_AND_ASSIGN(
      int error_code,
      api_->sapi_uv_os_homedir(uv_homedir.PtrBoth(), uv_homedir_len.PtrBoth()));
  ASSERT_GE(error_code, 0);

  // Test home directory is as expected
  ASSERT_EQ(std::string{uv_homedir.GetData()}, std::string{expected_homedir});
}

TEST_F(UVTestOS, HomeDirSmall) {
  // Try getting expected home directory, error because array is too small
  char expected_homedir[kSmallBufLen];
  size_t len = sizeof(expected_homedir);
  int expected_error_code = uv_os_homedir(expected_homedir, &len);
  ASSERT_NE(expected_error_code, 0);

  // Try getting home directory from sandbox, error because array is too small
  sapi::v::Array<char> uv_homedir(kSmallBufLen);
  uv_homedir[0] = '\0';
  sapi::v::IntBase<size_t> uv_homedir_len(kSmallBufLen);
  SAPI_ASSERT_OK_AND_ASSIGN(
      int error_code,
      api_->sapi_uv_os_homedir(uv_homedir.PtrBoth(), uv_homedir_len.PtrBoth()));
  ASSERT_NE(error_code, 0);

  // Test error code is as expected
  ASSERT_EQ(error_code, expected_error_code);
}

TEST_F(UVTestOS, TmpDirBig) {
  // Get expected tmp directory
  char expected_tmpdir[kBigBufLen];
  size_t len = sizeof(expected_tmpdir);
  ASSERT_GE(uv_os_tmpdir(expected_tmpdir, &len), 0);

  // Get tmp directory from the sandbox
  sapi::v::Array<char> uv_tmpdir(kBigBufLen);
  uv_tmpdir[0] = '\0';
  sapi::v::IntBase<size_t> uv_tmpdir_len(kBigBufLen);
  SAPI_ASSERT_OK_AND_ASSIGN(
      int error_code,
      api_->sapi_uv_os_tmpdir(uv_tmpdir.PtrBoth(), uv_tmpdir_len.PtrBoth()));
  ASSERT_GE(error_code, 0);

  // Test tmp directory is as expected
  ASSERT_EQ(std::string{uv_tmpdir.GetData()}, std::string{expected_tmpdir});
}

TEST_F(UVTestOS, TmpDirSmall) {
  // Try getting expected tmp directory, error because array is too small
  char expected_tmpdir[kSmallBufLen];
  size_t len = sizeof(expected_tmpdir);
  int expected_error_code = uv_os_tmpdir(expected_tmpdir, &len);
  ASSERT_NE(expected_error_code, 0);

  // Try getting tmp directory from sandbox, error because array is too small
  sapi::v::Array<char> uv_tmpdir(kSmallBufLen);
  uv_tmpdir[0] = '\0';
  sapi::v::IntBase<size_t> uv_tmpdir_len(kSmallBufLen);
  SAPI_ASSERT_OK_AND_ASSIGN(
      int error_code,
      api_->sapi_uv_os_tmpdir(uv_tmpdir.PtrBoth(), uv_tmpdir_len.PtrBoth()));
  ASSERT_NE(error_code, 0);

  // Test error code is as expected
  ASSERT_EQ(error_code, expected_error_code);
}

}  // namespace
