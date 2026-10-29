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

// A binary to test sandbox2 limits.
// Per setrlimit(2): exceeding RLIMIT_AS with mmap, brk or mremap do not
// kill but fail with ENOMEM. However if we trigger automatic stack
// expansion, for instance with a large stack allocation with alloca(3),
// and we have no alternate stack, then we are killed with SIGSEGV.

#include <sys/mman.h>
#include <syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>

int TestMmap() {
  // Regular read-write anonymous mapping.
  if (mmap(nullptr, 1ULL << 20 /* 1 MiB */, PROT_READ | PROT_WRITE,
           MAP_ANONYMOUS | MAP_SHARED, -1, 0) == MAP_FAILED) {
    return EXIT_FAILURE;
  }

  // Try to map RWX, should result in a violation by default.
  if (mmap(nullptr, 1ULL << 20 /* 1 MiB */, PROT_READ | PROT_WRITE | PROT_EXEC,
           MAP_ANONYMOUS | MAP_SHARED, -1, 0) == MAP_FAILED) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int TestMprotect() {
  void* addr = mmap(nullptr, 1ULL << 20 /* 1 MiB */, PROT_READ | PROT_WRITE,
                    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (addr == MAP_FAILED) {
    return EXIT_FAILURE;
  }
  if (mprotect(addr, 1ULL << 20 /* 1 MiB */, PROT_WRITE) != 0) {
    return EXIT_FAILURE;
  }
  if (mprotect(addr, 1ULL << 20 /* 1 MiB */,
               PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int TestPkeyMprotect() {
  void* addr = mmap(nullptr, 1ULL << 20 /* 1 MiB */, PROT_READ | PROT_WRITE,
                    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (addr == MAP_FAILED) {
    return EXIT_FAILURE;
  }
  if (syscall(__NR_pkey_mprotect, addr, 1ULL << 20 /* 1 MiB */, PROT_WRITE,
              -1) != 0 &&
      errno != ENOSYS) {
    return EXIT_FAILURE;
  }
  if (syscall(__NR_pkey_mprotect, addr, 1ULL << 20 /* 1 MiB */,
              PROT_READ | PROT_WRITE | PROT_EXEC, -1) != 0 &&
      errno != ENOSYS) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
  // Disable buffering.
  setbuf(stdin, nullptr);
  setbuf(stdout, nullptr);
  setbuf(stderr, nullptr);

  if (argc < 2) {
    printf("argc < 2\n");
    return EXIT_FAILURE;
  }

  int testno = atoi(argv[1]);  // NOLINT
  switch (testno) {
    case 1:
      return TestMmap();
    case 2:
      return TestMprotect();
    case 3:
      return TestPkeyMprotect();
    default:
      printf("Unknown test: %d\n", testno);
      return EXIT_FAILURE;
  }
}
