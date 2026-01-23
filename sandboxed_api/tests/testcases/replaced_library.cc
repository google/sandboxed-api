// Copyright 2025 Google LLC
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

#include "sandboxed_api/tests/testcases/replaced_library.h"

#include <err.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "absl/strings/string_view.h"

bool mylib_is_sandboxed() {
  // Magic sandbox2 syscall number.
  // Note: we don't use sandbox2::unit::IsRunningInSandbox2 b/c it pulls in
  // too many dependencies and disturbs the policy too much.
  return syscall(0x2f000fdb) == -1 && errno == 0xfdb;
}

void mylib_scalar_types(int a0, float a1, double a2, int64_t a3, char a4,
                        bool a5, size_t a6) {}

std::string mylib_copy(const std::string& src) { return src; }

void mylib_copy(absl::string_view src, std::string& dst) {
  dst.assign(src.data(), src.size());
}

void mylib_copy_raw(const char* src, char* dst, size_t size) {
  memcpy(dst, src, size);
}

int mylib_add(int x, int y) { return x + y; }

// Sanitizer instrumentation may break argument value tracking.
// In particular, ASan emits a call to __asan_memset to zero ev.
static __attribute__((noinline, disable_sanitizer_instrumentation)) void
mylib_epoll_ctl(int cmd) {
  // Use epoll_ctl as test syscall b/c it's not used otherwise (e.g. by libc)
  // and has subcommands. Also uninline it to make allowed command tracking
  // a bit more difficult.
  epoll_event ev = {};
  int ret = syscall(SYS_epoll_ctl, -1, cmd, -1, &ev);
  if (ret == 0 || errno != EBADF)
    errx(1, "epoll_ctl did not fail as expected: ret=%d, errno=%d", ret, errno);
}

void mylib_expected_syscall1() { mylib_epoll_ctl(EPOLL_CTL_ADD); }

void mylib_expected_syscall2() { mylib_epoll_ctl(EPOLL_CTL_DEL); }

void mylib_unexpected_syscall1() {
  epoll_event ev = {};
  // Hide the syscall number via a volatile access, syscall extractor won't
  // discover it since it does not track memory accesses. So EPOLL_CTL_MOD
  // should end up being prohibited (while ADD/DEL should be allowed).
  static volatile int nr = SYS_epoll_ctl;
  int ret = syscall(nr, -1, EPOLL_CTL_MOD, -1, &ev);
  if (ret == 0 || errno != EBADF)
    errx(1, "epoll_ctl did not fail as expected: ret=%d, errno=%d", ret, errno);
}

void mylib_unexpected_syscall2() {
  // This syscall should be prohibited (nothing else in the binary should use
  // this esoteric syscall).
  static volatile int nr = SYS_ioprio_get;
  if (syscall(nr, 1, 0)) errx(1, "ioprio_get failed");
}
