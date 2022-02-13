// Copyright 2022 Google LLC
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

#include <magic.h>

#include "file_sapi.h"       // NOLINT(build/include)
#include "file_sapi.sapi.h"  // NOLINT(build/include)
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"

namespace {

using ::sapi::IsOk;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::StrEq;

class FileSapiSandboxTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    ASSERT_THAT(getenv("TEST_FILES_DIR"), NotNull());
    sandbox_ = new FileSapiSandbox();
    ASSERT_THAT(sandbox_->Init(), IsOk());
    api_ = new file_sapi::FileApi(sandbox_);
  }
  static void TearDownTestSuite() {
    delete api_;
    delete sandbox_;
  }
  static std::string GetMagicErrorStr(sapi::v::Ptr* handle) {
    auto errmsg_ptr = api_->magic_error(handle);
    if (!errmsg_ptr.ok()) return "Error getting error message";
    auto errmsg =
        sandbox_->GetCString(sapi::v::RemotePtr(errmsg_ptr.value()), 256);
    if (!errmsg.ok()) return "Error getting error message";
    return errmsg.value();
  }
  static file_sapi::FileApi* api_;
  static FileSapiSandbox* sandbox_;
};

file_sapi::FileApi* FileSapiSandboxTest::api_;
FileSapiSandbox* FileSapiSandboxTest::sandbox_;

TEST_F(FileSapiSandboxTest, Open) {
  auto magic = api_->magic_open(MAGIC_PRESERVE_ATIME | MAGIC_ERROR);
  ASSERT_THAT(magic, IsOk());
  ASSERT_THAT(magic.value(), NotNull());
  auto magic_p = sapi::v::RemotePtr{magic.value()};

  sapi::v::Fd fd(open("/proc/self/exe", O_NOCTTY | O_RDONLY | O_CLOEXEC));
  ASSERT_NE(fd.GetValue(), -1);
  ASSERT_THAT(sandbox_->TransferToSandboxee(&fd), IsOk());
  sapi::v::NullPtr null;
  auto load_res = api_->magic_load(&magic_p, &null);
  ASSERT_THAT(load_res, IsOk());
  if (load_res.value()) {
    std::cerr << GetMagicErrorStr(&magic_p) << std::endl;
    FAIL();
  }
  std::cerr << "Loaded!" << std::endl;

  absl::StatusOr<char*> result =
      api_->magic_descriptor(&magic_p, fd.GetRemoteFd());
  std::cerr << "Call made!" << std::endl;
  ASSERT_THAT(result, IsOk());
  if (result.value() == nullptr) {
    std::cerr << GetMagicErrorStr(&magic_p) << std::endl;
    FAIL();
  }
  std::cerr << "Call succeeded!" << std::endl;
  auto msg = sandbox_->GetCString(sapi::v::RemotePtr(*result), 256);
  ASSERT_THAT(msg, IsOk());
  std::cout << *msg << std::endl;
  ASSERT_EQ(api_->magic_close(&magic_p).code(), absl::StatusCode::kOk);
  std::cerr << "Handle closed!" << std::endl;
}

}  // namespace

int main(int argc, char* argv[]) {
  ::google::InitGoogleLogging(program_invocation_short_name);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
