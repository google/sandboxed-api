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

#include <cerrno>
#include <initializer_list>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/allowlists/unrestricted_networking.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sandbox2 {

class PolicyBuilderPeer {
 public:
  explicit PolicyBuilderPeer(PolicyBuilder* builder) : builder_{builder} {}

  int policy_size() const { return builder_->user_policy_.size(); }

 private:
  PolicyBuilder* builder_;
};

namespace {

namespace fileops = ::sapi::file_util::fileops;

using ::sapi::IsOk;
using ::sapi::StatusIs;
using ::testing::Eq;
using ::testing::Lt;
using ::testing::StartsWith;
using ::testing::StrEq;

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
  builder.AllowTCGETS(); assert_same();
  builder.AllowTCGETS(); assert_same();

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
  builder.UseForkServerSharedNetNs(); assert_same();
  builder.Allow(UnrestrictedNetworking()); assert_same();
  // clang-format on
}

TEST(PolicyBuilderTest, ApisWithPathValidation) {
  const std::initializer_list<std::pair<absl::string_view, absl::StatusCode>>
      kTestCases = {
          {"/a", absl::StatusCode::kOk},
          {"/a/b/c/d", absl::StatusCode::kOk},
          {"/a/b/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", absl::StatusCode::kOk},
          {"", absl::StatusCode::kInvalidArgument},
          // Fails because we reject paths starting with '..'
          {"..", absl::StatusCode::kInvalidArgument},
          {"..a", absl::StatusCode::kInvalidArgument},
          {"../a", absl::StatusCode::kInvalidArgument},
          // Fails because is not absolute
          {"a", absl::StatusCode::kInvalidArgument},
          {"a/b", absl::StatusCode::kInvalidArgument},
          {"a/b/c", absl::StatusCode::kInvalidArgument},
          // Fails because '..' in path
          {"/a/b/c/../d", absl::StatusCode::kInvalidArgument},
          // Fails because '.' in path
          {"/a/b/c/./d", absl::StatusCode::kInvalidArgument},
          // Fails because '//' in path
          {"/a/b/c//d", absl::StatusCode::kInvalidArgument},
          // Fails because path ends with '/'
          {"/a/b/c/d/", absl::StatusCode::kInvalidArgument},
      };
  for (auto const& [path, status] : kTestCases) {
    EXPECT_THAT(PolicyBuilder().AddFile(path).TryBuild(), StatusIs(status));
    EXPECT_THAT(PolicyBuilder().AddFileAt(path, "/input").TryBuild(),
                StatusIs(status));
    EXPECT_THAT(PolicyBuilder().AddDirectory(path).TryBuild(),
                StatusIs(status));
    EXPECT_THAT(PolicyBuilder().AddDirectoryAt(path, "/input").TryBuild(),
                StatusIs(status));
  }

  // Fails because it attempts to mount to '/' inside
  EXPECT_THAT(PolicyBuilder().AddFile("/").TryBuild(),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(PolicyBuilder().AddDirectory("/").TryBuild(),
              StatusIs(absl::StatusCode::kInternal));

  // Succeeds because it attempts to mount to '/' inside
  EXPECT_THAT(PolicyBuilder().AddFileAt("/a", "/input").TryBuild(), IsOk());
  EXPECT_THAT(PolicyBuilder().AddDirectoryAt("/a", "/input").TryBuild(),
              IsOk());
}

TEST(PolicyBuilderTest, TestAnchorPathAbsolute) {
  const std::initializer_list<
      std::tuple<absl::string_view, absl::string_view, std::string>>
      kTestCases = {
          // relative_path is empty:
          {"", "/base", ""},  // Error: relative path is empty
          {"", "", ""},       // Error: relative path is empty

          // relative_path is absolute:
          {"/a/b/c/d", "/base", "/a/b/c/d"},
          {"/a/../../../../../etc/passwd", "/base",
           "/a/../../../../../etc/passwd"},
          {"/a/b/c/d", "base", "/a/b/c/d"},
          {"/a/b/c/d", "", "/a/b/c/d"},

          // base is absolute:
          {"a/b/c/d", "/base", "/base/a/b/c/d"},
          {"a/b/c/d/", "/base", "/base/a/b/c/d"},
          {"a/b/c//d", "/base", "/base/a/b/c/d"},
          {"a/b/../d/", "/base", "/base/a/d"},
          {"a/./b/c/", "/base", "/base/a/b/c"},
          {"./a/b/c/", "/base", "/base/a/b/c"},
          {"..foobar", "/base", "/base/..foobar"},
          {"a/b/c/d", "/base/../foo/bar",
           "/foo/bar/a/b/c/d"},         // Not an error because base is trusted.
          {"a/../../d/", "/base", ""},  // Error: can't guarantee anchor
          {"../a/b/c/", "/base", ""},   // Error: can't guarantee anchor
          {"..", "/base", ""},          // Error: can't guarantee anchor

          // base path is empty:
          {"a/b/c", "", fileops::GetCWD() + "/a/b/c"},
          {"a/../../../../c", "", ""},  // Error: can't guarantee anchor

          // base is relative:
          {"a/b/c/d", "base", fileops::GetCWD() + "/base/a/b/c/d"},
          {"a/b/c/d/", "base", fileops::GetCWD() + "/base/a/b/c/d"},
          {"a/b/c//d", "base", fileops::GetCWD() + "/base/a/b/c/d"},
          {"a/b/../d/", "base", fileops::GetCWD() + "/base/a/d"},
          {"a/./b/c/", "base", fileops::GetCWD() + "/base/a/b/c"},
          {"./a/b/c/", "base", fileops::GetCWD() + "/base/a/b/c"},
          {"..foobar", "base", fileops::GetCWD() + "/base/..foobar"},
          {"a/../../d/", "base", ""},  // Error: can't guarantee anchor
          {"../a/b/c/", "base", ""},   // Error: can't guarantee anchor
          {"..", "base", ""},          // Error: can't guarantee anchor
          {"a/b/c", ".base/foo/", fileops::GetCWD() + "/.base/foo/a/b/c"},
          {"a/b/c", "./base/foo", fileops::GetCWD() + "/base/foo/a/b/c"},
          {"a/b/c", "base/foo/../bar", fileops::GetCWD() + "/base/bar/a/b/c"},
          {"a/b/c", "base/foo//bar/",
           fileops::GetCWD() + "/base/foo/bar/a/b/c"},
          {"a/b/c", "..base/foo", fileops::GetCWD() + "/..base/foo/a/b/c"},
          {"a/b/c", "../base/foo",
           sapi::file::CleanPath(fileops::GetCWD() + "/../base/foo/a/b/c")},
          {"a/b/c", "..",
           sapi::file::CleanPath(fileops::GetCWD() + "/../a/b/c")},
      };
  for (auto const& [path, base, result] : kTestCases) {
    EXPECT_THAT(PolicyBuilder::AnchorPathAbsolute(path, base), StrEq(result));
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

TEST(PolicyBuilderTest, CanBypassPtrace) {
  PolicyBuilder builder;
  builder.AddPolicyOnSyscall(__NR_ptrace, {ALLOW})
      .BlockSyscallWithErrno(__NR_ptrace, ENOENT);
  EXPECT_THAT(builder.TryBuild(), Not(IsOk()));
}

TEST(PolicyBuilderTest, AddPolicyOnSyscallsNoEmptyList) {
  PolicyBuilder builder;
  builder.AddPolicyOnSyscalls({}, {ALLOW});
  EXPECT_THAT(builder.TryBuild(), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(PolicyBuilderTest, AddPolicyOnSyscallJumpOutOfBounds) {
  PolicyBuilder builder;
  builder.AddPolicyOnSyscall(__NR_write,
                             {BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 1, 2, 0)});
  EXPECT_THAT(builder.TryBuild(), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(PolicyBuilderTest, TestAllowLlvmCoverage) {
  ASSERT_THAT(setenv("COVERAGE", "1", 0), Eq(0));
  ASSERT_THAT(setenv("COVERAGE_DIR", "/tmp", 0), Eq(0));
  PolicyBuilder builder;
  builder.AllowLlvmCoverage();
  EXPECT_THAT(builder.TryBuild(), IsOk());
  ASSERT_THAT(unsetenv("COVERAGE"), Eq(0));
  ASSERT_THAT(unsetenv("COVERAGE_DIR"), Eq(0));
}

TEST(PolicyBuilderTest, TestAllowLlvmCoverageWithoutCoverageDir) {
  ASSERT_THAT(setenv("COVERAGE", "1", 0), Eq(0));
  PolicyBuilder builder;
  builder.AllowLlvmCoverage();
  EXPECT_THAT(builder.TryBuild(), IsOk());
  ASSERT_THAT(unsetenv("COVERAGE"), Eq(0));
}
}  // namespace
}  // namespace sandbox2
