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

#include "sandboxed_api/sandbox2/syscall.h"

#include <linux/unistd.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/config.h"

using ::testing::Eq;
using ::testing::StartsWith;

namespace sandbox2 {
namespace {

TEST(SyscallTest, Basic) {
  Syscall::Args args{1, 0xbadbeef, 5};
  Syscall syscall(Syscall::GetHostArch(), __NR_read, args);

  EXPECT_THAT(syscall.pid(), Eq(-1));
  EXPECT_THAT(syscall.arch(), Eq(Syscall::GetHostArch()));
  EXPECT_THAT(syscall.nr(), Eq(__NR_read));
  EXPECT_THAT(syscall.args(), Eq(args));
  EXPECT_THAT(syscall.stack_pointer(), Eq(0));
  EXPECT_THAT(syscall.instruction_pointer(), Eq(0));

  EXPECT_THAT(syscall.GetName(), Eq("read"));
  auto arg_desc = syscall.GetArgumentsDescription();
  EXPECT_THAT(arg_desc.size(), Eq(3));
  EXPECT_THAT(arg_desc[0], Eq("0x1 [1]"));
  EXPECT_THAT(arg_desc[1], Eq("0xbadbeef"));
  EXPECT_THAT(arg_desc[2], Eq("0x5 [5]"));
  EXPECT_THAT(syscall.GetDescription(),
              Eq(absl::StrCat(
                  Syscall::GetArchDescription(sapi::host_cpu::Architecture()),
                  " read [", __NR_read,
                  "](0x1 [1], 0xbadbeef, 0x5 [5]) IP: 0, STACK: 0")));
}

TEST(SyscallTest, Empty) {
  Syscall syscall;

  EXPECT_THAT(syscall.arch(), Eq(sapi::cpu::kUnknown));
  EXPECT_THAT(syscall.GetName(), StartsWith("UNKNOWN"));
  EXPECT_THAT(syscall.GetArgumentsDescription().size(), Eq(Syscall::kMaxArgs));
}

}  // namespace
}  // namespace sandbox2
