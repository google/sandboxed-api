// Copyright 2019 Google LLC
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

// Unit tests for crc4sandbox example.

#include <unistd.h>

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sandbox2 {
namespace {

using ::sapi::GetTestSourcePath;
using ::testing::Eq;
using ::testing::HasSubstr;

class CRC4Test : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = GetTestSourcePath("sandbox2/examples/crc4/crc4sandbox");
    env_ = util::CharPtrArray(environ).ToStringVector();
  }

  std::string path_;
  std::vector<std::string> env_;
};

// Test that crc4sandbox works.
TEST_F(CRC4Test, TestNormalOperation) {
  SKIP_SANITIZERS_AND_COVERAGE;
  std::string output;
  SAPI_ASSERT_OK_AND_ASSIGN(
      int exit_code,
      util::Communicate({path_, "-input", "ABCD"}, env_, &output));

  EXPECT_THAT(output, HasSubstr("0x44434241\n"));
  EXPECT_THAT(exit_code, Eq(0));
}

// Test that crc4sandbox protects against bugs, because only the sandboxee
// will crash and break its communication with executor.
TEST_F(CRC4Test, TestExploitAttempt) {
  SKIP_SANITIZERS_AND_COVERAGE;

  std::string output;
  SAPI_ASSERT_OK_AND_ASSIGN(
      int exit_code, util::Communicate({path_, "-input", std::string(128, 'A')},
                                       env_, &output));

  LOG(INFO) << "Output: " << output;
  EXPECT_THAT(exit_code, Eq(3));
}

// Test that if sandboxee calls a syscall that is not allowed by the policy,
// it triggers a policy violation for the executor.
TEST_F(CRC4Test, TestSyscallViolation) {
  SKIP_SANITIZERS_AND_COVERAGE;

  std::string output;
  SAPI_ASSERT_OK_AND_ASSIGN(
      int exit_code,
      util::Communicate({path_, "-input", "x", "-call_syscall_not_allowed"},
                        env_, &output));

  LOG(INFO) << "Output: " << output;
  EXPECT_THAT(exit_code, Eq(3));
}

}  // namespace
}  // namespace sandbox2
