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

// A binary that calls vfork() to test PTRACE_EVENT_VFORK and the Notify API.

#include <sched.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>

#include "sandboxed_api/config.h"

int main(int argc, char* argv[]) {
  errno = 0;
  pid_t pid;
  if constexpr (sapi::sanitizers::IsTSan()) {
    // TSan's vfork() interceptor turns vfork() into fork(), which does not
    // trigger PTRACE_EVENT_VFORK.
    pid = syscall(__NR_clone, CLONE_VFORK | SIGCHLD, 0, nullptr, nullptr, 0);
  } else {
    pid = vfork();
  }
  if (pid == -1) {
    return errno;
  }
  if (pid == 0) {
    _exit(0);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) != pid) {
    return EXIT_FAILURE;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
