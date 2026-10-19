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

#include <syscall.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/syscall_defs.h"
#include "sandboxed_api/testing.h"

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::Optional;
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
  auto arg_data = syscall.GetArgumentsData();
  ASSERT_THAT(arg_data.size(), Eq(3));
  EXPECT_THAT(arg_data[0].type(), Eq(syscalls::kResource));
  EXPECT_THAT(arg_data[0].pid(), Eq(-1));
  EXPECT_THAT(arg_data[0].value(), Eq(1));
  EXPECT_THAT(arg_data[0].length(), Eq(std::nullopt));
  EXPECT_THAT(arg_data[1].type(), Eq(syscalls::kArray));
  EXPECT_THAT(arg_data[1].pid(), Eq(-1));
  EXPECT_THAT(arg_data[1].value(), Eq(0xbadbeef));
  EXPECT_THAT(arg_data[1].length(), Optional(5));
  EXPECT_THAT(arg_data[2].type(), Eq(syscalls::kLen1));
  EXPECT_THAT(arg_data[2].pid(), Eq(-1));
  EXPECT_THAT(arg_data[2].value(), Eq(5));
  EXPECT_THAT(arg_data[2].length(), Eq(std::nullopt));
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
  EXPECT_THAT(syscall.GetArgumentsData().size(), Eq(Syscall::kMaxArgs));
  EXPECT_THAT(syscall.GetArgumentsDescription().size(), Eq(Syscall::kMaxArgs));
}

TEST(SyscallTest, GetEntry) {
  SyscallTable table = SyscallTable::get(sapi::host_cpu::Architecture());
  SAPI_ASSERT_OK_AND_ASSIGN(SyscallTable::Entry entry, table.GetEntry("read"));
  EXPECT_THAT(entry.nr, Eq(__NR_read));
  EXPECT_THAT(entry.name, Eq("read"));
  EXPECT_THAT(entry.num_args, Eq(3));
  EXPECT_THAT(table.GetEntry("non_existent_syscall"),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST(SyscallTest, VariousArgDescriptions) {
  Syscall::Args args{AF_UNIX, 0, 0, 0xdeadbeef};
  Syscall syscall(Syscall::GetHostArch(), __NR_socketpair, args);
  EXPECT_THAT(syscall.GetDescription(),
              Eq(absl::StrCat(
                  Syscall::GetArchDescription(sapi::host_cpu::Architecture()),
                  " socketpair [", __NR_socketpair, "](0x", absl::Hex(AF_UNIX),
                  " [AF_UNIX], 0 [\\00], 0 [0], 0xdeadbeef) IP: 0, STACK: 0")));
  Syscall::Args args2{0, SIGKILL};
  Syscall syscall2(Syscall::GetHostArch(), __NR_kill, args2);
  EXPECT_THAT(syscall2.GetDescription(),
              Eq(absl::StrCat(
                  Syscall::GetArchDescription(sapi::host_cpu::Architecture()),
                  " kill [", __NR_kill, "](0 [0], 0x", absl::Hex(SIGKILL),
                  " [SIGKILL [", SIGKILL, "]]) IP: 0, STACK: 0")));
  absl::string_view hostname = "test_hostname";
  seccomp_data str_data = {
      .nr = __NR_sethostname,
      .arch = Syscall::GetHostAuditArch(),
      .args = {reinterpret_cast<uint64_t>(hostname.data()), hostname.size()}};
  Syscall syscall3(getpid(), str_data);
  auto str_arg_data = syscall3.GetArgumentsData();
  ASSERT_THAT(str_arg_data.size(), Eq(2));
  EXPECT_THAT(str_arg_data[0].ReadAsString(), IsOkAndHolds(hostname));
  EXPECT_THAT(syscall3.GetDescription(),
              Eq(absl::StrCat(
                  Syscall::GetArchDescription(sapi::host_cpu::Architecture()),
                  " sethostname [", __NR_sethostname, "](0x",
                  absl::Hex(reinterpret_cast<uint64_t>(hostname.data())),
                  " [\"test_hostname\"], 0x", absl::Hex(hostname.size()), " [",
                  hostname.size(), "]) IP: 0, STACK: 0")));
}

TEST(SyscallTest, ArchDescription) {
  EXPECT_THAT(Syscall::GetArchDescription(sapi::cpu::kX8664), Eq("[X86-64]"));
  EXPECT_THAT(Syscall::GetArchDescription(sapi::cpu::kX86), Eq("[X86-32]"));
  EXPECT_THAT(Syscall::GetArchDescription(sapi::cpu::kPPC64LE), Eq("[PPC-64]"));
  EXPECT_THAT(Syscall::GetArchDescription(sapi::cpu::kArm64), Eq("[Arm-64]"));
  EXPECT_THAT(Syscall::GetArchDescription(sapi::cpu::kArm), Eq("[Arm-32]"));
  EXPECT_THAT(Syscall::GetArchDescription(sapi::cpu::kUnknown),
              StartsWith("[UNKNOWN_ARCH"));
}

}  // namespace
}  // namespace sandbox2
