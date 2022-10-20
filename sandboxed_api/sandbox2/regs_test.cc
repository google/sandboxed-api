#include "sandboxed_api/sandbox2/regs.h"

#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "sandboxed_api/sandbox2/sanitizer.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sandbox2 {
namespace {

using ::sapi::IsOk;

#define __WPTRACEEVENT(x) ((x & 0xff0000) >> 16)

TEST(RegsTest, SkipSyscallWorks) {
  std::vector<sock_filter> policy = {
      LOAD_SYSCALL_NR,
      JEQ32(__NR_getpid, TRACE(0)),
      ALLOW,
  };
  sock_fprog prog = {
      .len = static_cast<uint8_t>(policy.size()),
      .filter = policy.data(),
  };
  // Create socketpair for synchronization
  int sv[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  // Fork a child process to run the syscalls in.
  pid_t ppid = util::Syscall(__NR_gettid);
  pid_t pid = fork();
  ASSERT_NE(pid, -1);
  char c = 'C';
  if (pid == 0) {
    // Get ready for being ptraced.
    sanitizer::WaitForSanitizer();
    CHECK_EQ(prctl(PR_SET_DUMPABLE, 1), 0);
    prctl(PR_SET_PTRACER, ppid);
    CHECK_EQ(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0), 0);
    CHECK_EQ(prctl(PR_SET_KEEPCAPS, 0), 0);
    // Notify parent that we're ready for ptrace.
    CHECK_EQ(write(sv[0], &c, 1), 1);
    // Apply seccomp policy
    CHECK_EQ(util::Syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, 0,
                           reinterpret_cast<uintptr_t>(&prog)),
             0);
    // Wait for tracer to be attached.
    CHECK_EQ(read(sv[0], &c, 1), 1);
    // Run the test syscall
    errno = 0;
    util::Syscall(__NR_getpid, 123, reinterpret_cast<uintptr_t>(&c), 1);
    _Exit(errno == ENOENT ? 0 : 1);
  }
  // Wait for child to be ready for ptrace
  ASSERT_EQ(read(sv[1], &c, 1), 1);
  ASSERT_EQ(ptrace(PTRACE_SEIZE, pid, 0, PTRACE_O_TRACESECCOMP), 0);
  // Notify child it has been ptraced.
  ASSERT_EQ(write(sv[1], &c, 1), 1);
  // Wait for seccomp TRACE stop
  int status;
  ASSERT_EQ(waitpid(pid, &status, __WNOTHREAD | __WALL | WUNTRACED), pid);
  ASSERT_TRUE(WIFSTOPPED(status));
  ASSERT_EQ(__WPTRACEEVENT(status), PTRACE_EVENT_SECCOMP);
  // Fetch the registers
  Regs regs(pid);
  ASSERT_THAT(regs.Fetch(), IsOk());
  // Check syscall arguments
  Syscall syscall = regs.ToSyscall(sapi::host_cpu::Architecture());
  EXPECT_EQ(syscall.nr(), __NR_getpid);
  EXPECT_EQ(syscall.args()[0], 123);
  EXPECT_EQ(syscall.args()[1], reinterpret_cast<uintptr_t>(&c));
  EXPECT_EQ(syscall.args()[2], 1);
  // Skip syscall
  ASSERT_THAT(regs.SkipSyscallReturnValue(-ENOENT), IsOk());
  // Continue&detach the child process
  ASSERT_EQ(ptrace(PTRACE_DETACH, pid, 0, 0), 0);
  // Wait for the child to exit
  ASSERT_EQ(waitpid(pid, &status, __WNOTHREAD | __WALL | WUNTRACED), pid);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

}  // namespace
}  // namespace sandbox2
