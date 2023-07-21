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
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
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

using ::testing::Eq;
using ::testing::Lt;
using ::testing::StartsWith;
using ::testing::StrEq;
using ::sapi::IsOk;
using ::sapi::StatusIs;

TEST(PolicyBuilderTest, Testpolicy_size) {
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

  builder.AddPolicyOnSyscall(__NR_fchmod, { ALLOW }); assert_increased();
  builder.AddPolicyOnSyscall(__NR_fchmod, { ALLOW }); assert_increased();

  builder.AddPolicyOnSyscalls({ __NR_fchmod, __NR_chdir }, { ALLOW });
  assert_increased();
  builder.AddPolicyOnSyscalls({ __NR_fchmod, __NR_chdir }, { ALLOW });
  assert_increased();

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

TEST(PolicyBuilderTest, TestValidateAbsolutePath) {
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

TEST(PolicyBuilderTest, TestCanOnlyBuildOnce) {
  PolicyBuilder b;
  ASSERT_THAT(b.TryBuild(), IsOk());
  EXPECT_THAT(b.TryBuild(), StatusIs(absl::StatusCode::kFailedPrecondition,
                                     "Can only build policy once."));
}

TEST(PolicyBuilderTest, TestIsCopyable) {
  PolicyBuilder builder;
  builder.AllowSyscall(__NR_getpid);

  PolicyBuilder copy = builder;
  ASSERT_EQ(PolicyBuilderPeer(&copy).policy_size(),
            PolicyBuilderPeer(&builder).policy_size());

  // Both can be built.
  EXPECT_THAT(builder.TryBuild(), IsOk());
  EXPECT_THAT(copy.TryBuild(), IsOk());
}
}  // namespace
}  // namespace sandbox2
