// Copyright 2026 Google LLC
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

#include "sandboxed_api/sandbox2/util/syscall_trap.h"

#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <ucontext.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdint>

#include "gtest/gtest.h"
#include "absl/base/macros.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/thread.h"

#ifndef SYS_SECCOMP
constexpr int SYS_SECCOMP = 1;
#endif

namespace sandbox2 {
namespace {

constexpr int kTrappedSyscall = 0x7fff;

int g_old_handler_signal = 0;
int g_old_sigaction_signal = 0;

void OldSignalHandler(int nr) {
  g_old_handler_signal = nr;
  errno = EINVAL;
}

void OldSigactionHandler(int nr, siginfo_t* info, void* context) {
  g_old_sigaction_signal = nr;
  errno = EINVAL;
}

void InstallSeccompTrapForTestSyscall() {
  sock_filter filter[] = {
      LOAD_SYSCALL_NR,
      SYSCALL(kTrappedSyscall, TRAP(0)),
      ALLOW,
  };
  sock_fprog prog = {
      .len = ABSL_ARRAYSIZE(filter),
      .filter = filter,
  };
  CHECK_EQ(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0), 0);
  CHECK_EQ(prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog), 0);
}

TEST(SyscallTrapTest, TrapsSyscallAndDoubleInstallFails) {
  EXPECT_FALSE(SyscallTrap::Uninstall());
  ASSERT_TRUE(SyscallTrap::Install(
      [](int nr, SyscallTrap::Args args, uintptr_t* result) {
        if (nr == kTrappedSyscall && args[0] == 1 && args[1] == 2 &&
            args[2] == 3 && args[3] == 4 && args[4] == 5 && args[5] == 6) {
          *result = 42;
          return true;
        }
        return false;
      }));
  absl::Cleanup cleanup = [] { EXPECT_TRUE(SyscallTrap::Uninstall()); };

  EXPECT_FALSE(SyscallTrap::Install(
      [](int, SyscallTrap::Args, uintptr_t* result) { return false; }));

  // Install the seccomp filter on a dedicated thread so the main test thread's
  // seccomp state remains unaffected.
  sapi::Thread thread([] {
    InstallSeccompTrapForTestSyscall();
    EXPECT_EQ(syscall(kTrappedSyscall, uintptr_t{1}, uintptr_t{2}, uintptr_t{3},
                      uintptr_t{4}, uintptr_t{5}, uintptr_t{6}),
              42);
  });
  thread.Join();
}

TEST(SyscallTrapTest, UnhandledSyscallInvokesOldActSigDfl) {
  struct sigaction old_act = {};
  old_act.sa_handler = SIG_DFL;
  ASSERT_EQ(sigaction(SIGSYS, &old_act, nullptr), 0);

  ASSERT_TRUE(SyscallTrap::Install(
      [](int, SyscallTrap::Args, uintptr_t* result) { return false; }));
  absl::Cleanup cleanup = [] { EXPECT_TRUE(SyscallTrap::Uninstall()); };

  struct sigaction installed_act = {};
  ASSERT_EQ(sigaction(SIGSYS, nullptr, &installed_act), 0);
  ASSERT_NE(installed_act.sa_sigaction, nullptr);

  // Block SIGSYS on this thread so raise(SIGSYS) inside InvokeOldAct queues the
  // signal as pending rather than terminating the test process.
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGSYS);
  sigset_t old_mask;
  ASSERT_EQ(sigprocmask(SIG_BLOCK, &mask, &old_mask), 0);
  absl::Cleanup restore_mask = [&old_mask] {
    EXPECT_EQ(sigprocmask(SIG_SETMASK, &old_mask, nullptr), 0);
  };

  siginfo_t info = {};
  info.si_code = SYS_SECCOMP;
  ucontext_t uctx = {};
  errno = EAGAIN;
  installed_act.sa_sigaction(SIGSYS, &info, &uctx);
  EXPECT_EQ(errno, EAGAIN);

  struct sigaction restored_act = {};
  ASSERT_EQ(sigaction(SIGSYS, nullptr, &restored_act), 0);
  EXPECT_EQ(restored_act.sa_handler, SIG_DFL);

  int sig = 0;
  ASSERT_EQ(sigwait(&mask, &sig), 0);
  EXPECT_EQ(sig, SIGSYS);
}

TEST(SyscallTrapTest, InvokeOldActSigIgn) {
  struct sigaction old_act = {};
  old_act.sa_handler = SIG_IGN;
  ASSERT_EQ(sigaction(SIGSYS, &old_act, nullptr), 0);
  absl::Cleanup restore_dfl = [] {
    struct sigaction dfl_act = {};
    dfl_act.sa_handler = SIG_DFL;
    EXPECT_EQ(sigaction(SIGSYS, &dfl_act, nullptr), 0);
  };

  ASSERT_TRUE(SyscallTrap::Install(
      [](int, SyscallTrap::Args, uintptr_t* result) { return false; }));
  absl::Cleanup cleanup = [] { EXPECT_TRUE(SyscallTrap::Uninstall()); };

  errno = EAGAIN;
  EXPECT_EQ(raise(SIGSYS), 0);
  EXPECT_EQ(errno, EAGAIN);
}

TEST(SyscallTrapTest, InvokeOldActSaHandler) {
  struct sigaction old_act = {};
  old_act.sa_handler = &OldSignalHandler;
  ASSERT_EQ(sigaction(SIGSYS, &old_act, nullptr), 0);
  absl::Cleanup restore_dfl = [] {
    struct sigaction dfl_act = {};
    dfl_act.sa_handler = SIG_DFL;
    EXPECT_EQ(sigaction(SIGSYS, &dfl_act, nullptr), 0);
  };

  ASSERT_TRUE(SyscallTrap::Install(
      [](int, SyscallTrap::Args, uintptr_t* result) { return false; }));
  absl::Cleanup cleanup = [] { EXPECT_TRUE(SyscallTrap::Uninstall()); };

  g_old_handler_signal = 0;
  errno = EAGAIN;
  EXPECT_EQ(raise(SIGSYS), 0);
  EXPECT_EQ(g_old_handler_signal, SIGSYS);
  EXPECT_EQ(errno, EAGAIN);
}

TEST(SyscallTrapTest, InvokeOldActSaSigaction) {
  struct sigaction old_act = {};
  old_act.sa_sigaction = &OldSigactionHandler;
  old_act.sa_flags = SA_SIGINFO;
  ASSERT_EQ(sigaction(SIGSYS, &old_act, nullptr), 0);
  absl::Cleanup restore_dfl = [] {
    struct sigaction dfl_act = {};
    dfl_act.sa_handler = SIG_DFL;
    EXPECT_EQ(sigaction(SIGSYS, &dfl_act, nullptr), 0);
  };

  ASSERT_TRUE(SyscallTrap::Install(
      [](int, SyscallTrap::Args, uintptr_t* result) { return false; }));
  absl::Cleanup cleanup = [] { EXPECT_TRUE(SyscallTrap::Uninstall()); };

  g_old_sigaction_signal = 0;
  errno = EAGAIN;
  EXPECT_EQ(raise(SIGSYS), 0);
  EXPECT_EQ(g_old_sigaction_signal, SIGSYS);
  EXPECT_EQ(errno, EAGAIN);
}

TEST(SyscallTrapTest, InvokeOldActNullSaSigaction) {
  struct sigaction old_act = {};
  old_act.sa_sigaction = nullptr;
  old_act.sa_flags = SA_SIGINFO;
  ASSERT_EQ(sigaction(SIGSYS, &old_act, nullptr), 0);
  absl::Cleanup restore_dfl = [] {
    struct sigaction dfl_act = {};
    dfl_act.sa_handler = SIG_DFL;
    EXPECT_EQ(sigaction(SIGSYS, &dfl_act, nullptr), 0);
  };

  ASSERT_TRUE(SyscallTrap::Install(
      [](int, SyscallTrap::Args, uintptr_t* result) { return false; }));
  absl::Cleanup cleanup = [] { EXPECT_TRUE(SyscallTrap::Uninstall()); };

  errno = EAGAIN;
  EXPECT_EQ(raise(SIGSYS), 0);
  EXPECT_EQ(errno, EAGAIN);
}

TEST(SyscallTrapTest, NonSigsysAndNullUcontext) {
  struct sigaction old_act = {};
  old_act.sa_handler = &OldSignalHandler;
  ASSERT_EQ(sigaction(SIGSYS, &old_act, nullptr), 0);
  absl::Cleanup restore_dfl = [] {
    struct sigaction dfl_act = {};
    dfl_act.sa_handler = SIG_DFL;
    EXPECT_EQ(sigaction(SIGSYS, &dfl_act, nullptr), 0);
  };

  ASSERT_TRUE(SyscallTrap::Install(
      [](int, SyscallTrap::Args, uintptr_t* result) { return true; }));
  absl::Cleanup cleanup = [] { EXPECT_TRUE(SyscallTrap::Uninstall()); };

  struct sigaction installed_act = {};
  ASSERT_EQ(sigaction(SIGSYS, nullptr, &installed_act), 0);
  ASSERT_NE(installed_act.sa_sigaction, nullptr);

  // Non-SIGSYS signal invokes old action and preserves errno.
  g_old_handler_signal = 0;
  errno = EAGAIN;
  installed_act.sa_sigaction(SIGUSR1, nullptr, nullptr);
  EXPECT_EQ(g_old_handler_signal, SIGUSR1);
  EXPECT_EQ(errno, EAGAIN);

  // Null ucontext with SIGSYS and SYS_SECCOMP returns early and preserves
  // errno without invoking old action.
  g_old_handler_signal = 0;
  siginfo_t info = {};
  info.si_code = SYS_SECCOMP;
  errno = EAGAIN;
  installed_act.sa_sigaction(SIGSYS, &info, nullptr);
  EXPECT_EQ(g_old_handler_signal, 0);
  EXPECT_EQ(errno, EAGAIN);
}

}  // namespace
}  // namespace sandbox2
