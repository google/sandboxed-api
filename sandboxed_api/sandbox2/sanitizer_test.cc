// Copyright 2020 Google LLC. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sandboxed_api/sandbox2/sanitizer.h"

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <utility>
#include <vector>

#include <glog/logging.h>
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2/testing.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/status_matchers.h"

using ::testing::Eq;
using ::testing::Gt;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Ne;

namespace sandbox2 {
namespace {

// Runs a new process and returns 0 if the process terminated with 0.
int RunTestcase(const std::string& path, const std::vector<std::string>& args) {
  pid_t pid = fork();
  if (pid < 0) {
    PLOG(ERROR) << "fork()";
    return 1;
  }
  if (pid == 0) {
    const char** argv = util::VecStringToCharPtrArr(args);
    execv(path.c_str(), const_cast<char**>(argv));
    PLOG(ERROR) << "execv('" << path << "')";
    exit(EXIT_FAILURE);
  }

  for (;;) {
    int status;
    while (wait4(pid, &status, __WALL, nullptr) != pid) {
    }

    if (WIFEXITED(status)) {
      return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
      LOG(ERROR) << "PID: " << pid << " signaled with: " << WTERMSIG(status);
      return 128 + WTERMSIG(status);
    }
  }
}

// Test that marking file descriptors as close-on-exec works.
TEST(SanitizerTest, TestMarkFDsAsCOE) {
  // Open a few file descriptors in non-close-on-exec mode.
  int sock_fd[2];
  ASSERT_THAT(socketpair(AF_UNIX, SOCK_STREAM, 0, sock_fd), Ne(-1));
  ASSERT_THAT(open("/dev/full", O_RDONLY), Ne(-1));
  int null_fd = open("/dev/null", O_RDWR);
  ASSERT_THAT(null_fd, Ne(-1));

  const std::set<int> exceptions = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO,
                                    null_fd};
  ASSERT_THAT(sanitizer::MarkAllFDsAsCOEExcept(exceptions), IsTrue());

  const std::string path = GetTestSourcePath("sandbox2/testcases/sanitizer");
  std::vector<std::string> args;
  for (auto fd : exceptions) {
    args.push_back(absl::StrCat(fd));
  }
  ASSERT_THAT(RunTestcase(path, args), Eq(0));
}

// Test that default sanitizer leaves only 0/1/2 and 1023 (client comms FD)
// open but closes the rest.
TEST(SanitizerTest, TestSandboxedBinary) {
  SKIP_SANITIZERS_AND_COVERAGE;
  // Open a few file descriptors in non-close-on-exec mode.
  int sock_fd[2];
  ASSERT_THAT(socketpair(AF_UNIX, SOCK_STREAM, 0, sock_fd), Ne(-1));
  ASSERT_THAT(open("/dev/full", O_RDONLY), Ne(-1));
  ASSERT_THAT(open("/dev/null", O_RDWR), Ne(-1));

  const std::string path = GetTestSourcePath("sandbox2/testcases/sanitizer");
  std::vector<std::string> args = {
      absl::StrCat(STDIN_FILENO),
      absl::StrCat(STDOUT_FILENO),
      absl::StrCat(STDERR_FILENO),
      absl::StrCat(Comms::kSandbox2ClientCommsFD),
  };
  auto executor = absl::make_unique<Executor>(path, args);

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, PolicyBuilder()
                                        .DisableNamespaces()
                                        // Don't restrict the syscalls at all.
                                        .DangerDefaultAllowAll()
                                        .TryBuild());

  Sandbox2 s2(std::move(executor), std::move(policy));
  auto result = s2.Run();

  EXPECT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));
}

TEST(SanitizerTest, TestGetProcStatusLine) {
  // Test indirectly, GetNumberOfThreads() looks for the "Threads" value.
  EXPECT_THAT(sanitizer::GetNumberOfThreads(getpid()), Gt(0));
}

}  // namespace
}  // namespace sandbox2
