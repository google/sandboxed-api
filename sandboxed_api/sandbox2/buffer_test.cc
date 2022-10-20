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

#include "sandboxed_api/sandbox2/buffer.h"

#include <sys/stat.h>
#include <syscall.h>
#include <unistd.h>

#include <cerrno>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/ipc.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sandbox2 {
namespace {

using ::sapi::GetTestSourcePath;
using ::testing::Eq;
using ::testing::IsTrue;
using ::testing::Ne;

// Test all public methods of sandbox2::Buffer.
TEST(BufferTest, TestImplementation) {
  constexpr int kSize = 1024;
  SAPI_ASSERT_OK_AND_ASSIGN(auto buffer, Buffer::CreateWithSize(kSize));
  EXPECT_THAT(buffer->size(), Eq(kSize));
  uint8_t* raw_buf = buffer->data();
  for (int i = 0; i < kSize; i++) {
    raw_buf[i] = 'X';
  }
  SAPI_ASSERT_OK_AND_ASSIGN(auto buffer2, Buffer::CreateFromFd(buffer->fd()));
  uint8_t* raw_buf2 = buffer2->data();
  for (int i = 0; i < kSize; i++) {
    EXPECT_THAT(raw_buf2[i], Eq('X'));
  }
}

std::unique_ptr<Policy> BufferTestcasePolicy() {
  auto s2p = PolicyBuilder()
                 .DisableNamespaces()
                 .AllowDynamicStartup()
                 .AllowExit()
                 .AllowSafeFcntl()
                 .AllowTime()
                 .AllowSystemMalloc()
                 .AllowRead()
                 .AllowWrite()
                 .AllowMmap()
                 .AllowStat()
                 .AllowOpen()
                 .AllowSyscalls({
                     __NR_dup,
                     __NR_futex,
                     __NR_getpid,
                     __NR_gettid,
                     __NR_nanosleep,
                     __NR_rt_sigprocmask,
                     __NR_recvmsg,
                     __NR_lseek,
                     __NR_close,
                 })
                 .BlockSyscallsWithErrno(
                     {
#ifdef __NR_access
                         // On Debian, even static binaries check existence of
                         // /etc/ld.so.nohwcap.
                         __NR_access,
#endif
                         __NR_faccessat,
                     },
                     ENOENT)
                 .BlockSyscallWithErrno(__NR_prlimit64, EPERM)
                 .BuildOrDie();

  return s2p;
}

// Test sharing of buffer between executor/sandboxee using dup/MapFd.
TEST(BufferTest, TestWithSandboxeeMapFd) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path = GetTestSourcePath("sandbox2/testcases/buffer");
  std::vector<std::string> args = {path, "1"};
  auto executor = std::make_unique<Executor>(path, args);
  auto policy = BufferTestcasePolicy();

  SAPI_ASSERT_OK_AND_ASSIGN(auto buffer,
                            Buffer::CreateWithSize(1ULL << 20 /* 1MiB */));
  // buffer() uses the internal fd to mmap the buffer.
  uint8_t* buf = buffer->data();
  // Test that we can write data to the sandboxee.
  buf[0] = 'A';

  // Map buffer as fd 3, but careful because MapFd closes the buffer fd and
  // we need to keep it since buffer uses it for mmap, so we must dup.
  executor->ipc()->MapFd(dup(buffer->fd()), 3);

  Sandbox2 s2(std::move(executor), std::move(policy));
  auto result = s2.Run();

  EXPECT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));

  // Test that we can read data from the sandboxee.
  EXPECT_THAT(buf[buffer->size() - 1], Eq('B'));

  // Test that internal buffer fd remains valid.
  struct stat stat_buf;
  EXPECT_THAT(fstat(buffer->fd(), &stat_buf), Ne(-1));
}

// Test sharing of buffer between executor/sandboxee using SendFD/RecvFD.
TEST(BufferTest, TestWithSandboxeeSendRecv) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path = GetTestSourcePath("sandbox2/testcases/buffer");
  std::vector<std::string> args = {path, "2"};
  auto executor = std::make_unique<Executor>(path, args);

  Sandbox2 s2(std::move(executor), BufferTestcasePolicy());
  ASSERT_THAT(s2.RunAsync(), IsTrue());
  Comms* comms = s2.comms();

  SAPI_ASSERT_OK_AND_ASSIGN(auto buffer,
                            Buffer::CreateWithSize(1ULL << 20 /* 1MiB */));
  uint8_t* buf = buffer->data();
  // Test that we can write data to the sandboxee.
  buf[0] = 'A';
  EXPECT_THAT(comms->SendFD(buffer->fd()), IsTrue());

  auto result = s2.AwaitResult();
  EXPECT_THAT(result.final_status(), Eq(Result::OK));
  EXPECT_THAT(result.reason_code(), Eq(0));

  // Test that we can read data from the sandboxee.
  EXPECT_THAT(buf[buffer->size() - 1], Eq('B'));

  // Test that internal buffer fd remains valid.
  struct stat stat_buf;
  EXPECT_THAT(fstat(buffer->fd(), &stat_buf), Ne(-1));
}

}  // namespace
}  // namespace sandbox2
