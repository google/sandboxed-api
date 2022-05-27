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

// A binary that tries x86_64 compat syscalls, ptrace and clone untraced.

#include <sched.h>
#include <sys/ptrace.h>
#include <syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "sandboxed_api/config.h"

#ifdef SAPI_X86_64
void TestAMD64SyscallMismatch() {
  int64_t result;

  // exit() is allowed, but not if called via 32-bit syscall.
  asm("movq $1, %%rax\n"   // __NR_exit: 1 in 32-bit (60 in 64-bit)
      "movq $42, %%rbx\n"  // int error_code: 42
      "int $0x80\n"
      "movq %%rax, %0\n"
      : "=r"(result)
      :
      : "rax", "rbx");
  exit(-result);
}

void TestAMD64SyscallMismatchFs() {
  int64_t result;
  char filename[] = "/etc/passwd";

  // access("/etc/passwd") is allowed, but not if called via 32-bit syscall.
  asm("movq $33, %%rax\n"  // __NR_access: 33 in 32-bit (21 in 64-bit)
      "movq %1, %%rbx\n"   // const char* filename: /etc/passwd
      "movq $0, %%rcx\n"   // int mode: F_OK (0), test for existence
      "int $0x80\n"
      "movq %%rax, %0\n"
      : "=r"(result)
      : "g"(filename)
      : "rax", "rbx", "rcx");
  exit(-result);
}
#endif

void TestPtraceDenied() {
  ptrace(PTRACE_SEIZE, getppid(), 0, 0);

  printf("Syscall violation should have been discovered by now\n");
  exit(EXIT_FAILURE);
}

void TestPtraceBlocked() {
  int result = ptrace(PTRACE_SEIZE, getppid(), 0, 0);

  if (result != -1 || errno != EPERM) {
    printf("System call should have been blocked\n");
    exit(EXIT_FAILURE);
  }
}

void TestCloneUntraced() {
  syscall(__NR_clone, static_cast<uintptr_t>(CLONE_UNTRACED), nullptr, nullptr,
          nullptr, static_cast<uintptr_t>(0));

  printf("Syscall violation should have been discovered by now\n");
  exit(EXIT_FAILURE);
}

void TestBpf() {
  syscall(__NR_bpf, 0, nullptr, 0);

  printf("Syscall violation should have been discovered by now\n");
  exit(EXIT_FAILURE);
}

void TestBpfError() {
  exit(syscall(__NR_bpf, 0, nullptr, 0) == -1 ? errno : 0);
}

void TestIsatty() {
  isatty(0);

  printf("Syscall violation should have been discovered by now\n");
  exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
  // Disable buffering.
  setbuf(stdin, nullptr);
  setbuf(stdout, nullptr);
  setbuf(stderr, nullptr);

  if (argc < 2) {
    printf("argc < 3\n");
    return EXIT_FAILURE;
  }

  int testno = atoi(argv[1]);  // NOLINT
  switch (testno) {
#ifdef SAPI_X86_64
    case 1:
      TestAMD64SyscallMismatch();
      break;
    case 2:
      TestAMD64SyscallMismatchFs();
      break;
#endif
    case 3:
      TestPtraceDenied();
      break;
    case 4:
      TestCloneUntraced();
      break;
    case 5:
      TestBpf();
      break;
    case 6:
      TestIsatty();
      break;
    case 7:
      TestBpfError();
      break;
    case 8:
      TestPtraceBlocked();
      break;
    default:
      printf("Unknown test: %d\n", testno);
      return EXIT_FAILURE;
  }

  printf("OK: All tests went OK\n");
  return EXIT_SUCCESS;
}
