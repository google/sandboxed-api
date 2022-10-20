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

#include "sandboxed_api/sandbox2/policybuilder.h"

#include <syscall.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/ipc.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sandbox2 {

class PolicyBuilderPeer {
 public:
  explicit PolicyBuilderPeer(PolicyBuilder* builder) : builder_{builder} {}

  int policy_size() const { return builder_->user_policy_.size(); }

  static absl::StatusOr<std::string> ValidateAbsolutePath(
      absl::string_view path) {
    return PolicyBuilder::ValidateAbsolutePath(path);
  }

 private:
  PolicyBuilder* builder_;
};

namespace {

using ::sapi::GetTestSourcePath;
using ::testing::AllOf;
using ::testing::AnyOf;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::Lt;
using ::testing::NotNull;
using ::testing::StartsWith;
using ::testing::StrEq;
using ::sapi::IsOk;
using ::sapi::StatusIs;

class PolicyBuilderTest : public testing::Test {
 protected:
  static std::string Run(const std::vector<std::string>& args,
                         bool network = false);
};

TEST_F(PolicyBuilderTest, Testpolicy_size) {
  ssize_t last_size = 0;
  PolicyBuilder builder;
  PolicyBuilderPeer builder_peer{&builder};

  auto assert_increased = [&last_size, &builder_peer]() {
    ASSERT_THAT(last_size, Lt(builder_peer.policy_size()));
    last_size = builder_peer.policy_size();
  };

  auto assert_same = [&last_size, &builder_peer]() {
    ASSERT_THAT(last_size, Eq(builder_peer.policy_size()));
  };

  // clang-format off
  assert_same();

  builder.AllowSyscall(__NR_chroot); assert_increased();
  builder.AllowSyscall(__NR_chroot); assert_same();
  builder.AllowSyscall(__NR_umask); assert_increased();
  builder.AllowSyscall(__NR_umask); assert_same();
  builder.AllowSyscall(__NR_chroot); assert_same();
  builder.AllowSyscall(__NR_chroot); assert_same();

  builder.AllowSystemMalloc(); assert_increased();
  builder.AllowSyscall(__NR_munmap); assert_same();
  builder.BlockSyscallWithErrno(__NR_munmap, 1); assert_same();
  builder.BlockSyscallWithErrno(__NR_openat, 1);
  assert_increased();

  builder.AllowTCGETS(); assert_increased();
  builder.AllowTCGETS(); assert_increased();
  builder.AllowTCGETS(); assert_increased();

  builder.DangerDefaultAllowAll(); assert_increased();
  builder.DangerDefaultAllowAll(); assert_increased();
  builder.AddPolicyOnSyscall(__NR_fchmod, { ALLOW }); assert_increased();
  builder.AddPolicyOnSyscall(__NR_fchmod, { ALLOW }); assert_increased();

  builder.AddPolicyOnSyscalls({ __NR_fchmod, __NR_chdir }, { ALLOW });
  assert_increased();
  builder.AddPolicyOnSyscalls({ __NR_fchmod, __NR_chdir }, { ALLOW });
  assert_increased();
  builder.AddPolicyOnSyscalls({ }, { ALLOW }); assert_increased();

  // This might change in the future if we implement an optimization.
  builder.AddPolicyOnSyscall(__NR_umask, { ALLOW }); assert_increased();
  builder.AddPolicyOnSyscall(__NR_umask, { ALLOW }); assert_increased();

  // None of the namespace functions should alter the seccomp policy.
  builder.AddFile("/usr/bin/find"); assert_same();
  builder.AddDirectory("/bin"); assert_same();
  builder.AddTmpfs("/tmp", /*size=*/4ULL << 20 /* 4 MiB */); assert_same();
  builder.AllowUnrestrictedNetworking(); assert_same();
  // clang-format on
}

TEST_F(PolicyBuilderTest, TestValidateAbsolutePath) {
  for (auto const& bad_path : {
           "..",
           "a",
           "a/b",
           "a/b/c",
           "/a/b/c/../d",
           "/a/b/c/./d",
           "/a/b/c//d",
           "/a/b/c/d/",
           "/a/bAAAAAAAAAAAAAAAAAAAAAA/c/d/",
       }) {
    EXPECT_THAT(PolicyBuilderPeer::ValidateAbsolutePath(bad_path),
                StatusIs(absl::StatusCode::kInvalidArgument));
  }

  for (auto const& good_path :
       {"/", "/a/b/c/d", "/a/b/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"}) {
    SAPI_ASSERT_OK_AND_ASSIGN(
        std::string path, PolicyBuilderPeer::ValidateAbsolutePath(good_path));
    EXPECT_THAT(path, StrEq(good_path));
  }
}

std::string PolicyBuilderTest::Run(const std::vector<std::string>& args,
                                   bool network) {
  PolicyBuilder builder;
  // Don't restrict the syscalls at all.
  builder.DangerDefaultAllowAll();

  if constexpr (sapi::host_os::IsAndroid()) {
    builder.DisableNamespaces();
  } else {
    builder.AddLibrariesForBinary(args[0]);
  }

  if (network) {
    builder.AllowUnrestrictedNetworking();
  }

  auto executor = std::make_unique<sandbox2::Executor>(args[0], args);
  if constexpr (sapi::sanitizers::IsAny()) {
    executor->limits()->set_rlimit_as(RLIM64_INFINITY);
  }
  int fd1 = executor->ipc()->ReceiveFd(STDOUT_FILENO);
  sandbox2::Sandbox2 s2(std::move(executor), builder.BuildOrDie());

  s2.RunAsync();

  char buf[4096];
  std::string output;

  while (true) {
    int nbytes;
    PCHECK((nbytes = read(fd1, buf, sizeof(buf))) >= 0);

    if (nbytes == 0) break;
    output += std::string(buf, nbytes);
  }

  auto result = s2.AwaitResult();
  EXPECT_EQ(result.final_status(), sandbox2::Result::OK);
  return output;
}

TEST_F(PolicyBuilderTest, TestCanOnlyBuildOnce) {
  PolicyBuilder b;
  ASSERT_THAT(b.TryBuild(), IsOk());
  EXPECT_THAT(b.TryBuild(), StatusIs(absl::StatusCode::kFailedPrecondition,
                                     "Can only build policy once."));
}

TEST_F(PolicyBuilderTest, TestIsCopyable) {
  PolicyBuilder builder;
  builder.DangerDefaultAllowAll();

  PolicyBuilder copy = builder;
  ASSERT_EQ(PolicyBuilderPeer(&copy).policy_size(), 1);

  // Building both does not crash.
  builder.BuildOrDie();
  copy.BuildOrDie();
}

TEST_F(PolicyBuilderTest, TestEcho) {
  ASSERT_THAT(Run({"/bin/echo", "HELLO"}), StrEq("HELLO\n"));
}

TEST_F(PolicyBuilderTest, TestInterfacesNoNetwork) {
  SKIP_ANDROID;
  auto lines = absl::StrSplit(Run({"/sbin/ip", "addr", "show", "up"}), '\n');

  int count = 0;
  for (auto const& line : lines) {
    if (!line.empty() && !absl::StartsWith(line, " ")) {
      count += 1;
    }
  }

  // Only loopback network interface 'lo'.
  EXPECT_THAT(count, Eq(1));
}

TEST_F(PolicyBuilderTest, TestInterfacesNetwork) {
  SKIP_ANDROID;
  auto lines =
      absl::StrSplit(Run({"/sbin/ip", "addr", "show", "up"}, true), '\n');

  int count = 0;
  for (auto const& line : lines) {
    if (!line.empty() && !absl::StartsWith(line, " ")) {
      count += 1;
    }
  }

  // Loopback network interface 'lo' and more.
  EXPECT_THAT(count, Gt(1));
}

TEST_F(PolicyBuilderTest, TestUid) {
  if constexpr (!sapi::host_os::IsAndroid()) {
    EXPECT_THAT(Run({"/usr/bin/id", "-u"}), StrEq("1000\n"));
  } else {
    EXPECT_THAT(Run({"/bin/id", "-u"}), StrEq("0\n"));
  }
}

TEST_F(PolicyBuilderTest, TestGid) {
  if constexpr (!sapi::host_os::IsAndroid()) {
    EXPECT_THAT(Run({"/usr/bin/id", "-g"}), StrEq("1000\n"));
  } else {
    EXPECT_THAT(Run({"/bin/id", "-g"}), StrEq("0\n"));
  }
}

TEST_F(PolicyBuilderTest, TestOpenFds) {
  SKIP_SANITIZERS_AND_COVERAGE;

  std::string sandboxee = GetTestSourcePath("sandbox2/testcases/print_fds");
  std::string expected =
      absl::StrCat("0\n1\n2\n", sandbox2::Comms::kSandbox2ClientCommsFD, "\n");
  EXPECT_THAT(Run({sandboxee}), StrEq(expected));
}

}  // namespace
}  // namespace sandbox2
