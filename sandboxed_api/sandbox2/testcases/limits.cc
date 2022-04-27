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

// A binary to test sandbox2 limits.
// Per setrlimit(2): exceeding RLIMIT_AS with mmap, brk or mremap do not
// kill but fail with ENOMEM. However if we trigger automatic stack
// expansion, for instance with a large stack allocation with alloca(3),
// and we have no alternate stack, then we are killed with SIGSEGV.

#include <sys/mman.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>

int TestMmapUnderLimit(void) {
  // mmap should work
  void* ptr = mmap(0, 1ULL << 20 /* 1 MiB */, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_SHARED, -1, 0);
  if (ptr == MAP_FAILED) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int TestMmapAboveLimit(void) {
  // mmap should fail with ENOMEM
  void* ptr = mmap(0, 100ULL << 20 /* 100 MiB */, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_SHARED, -1, 0);
  if (ptr != MAP_FAILED || errno != ENOMEM) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

// Tests using alloca are marked noinline because clang in optimized mode tries
// to inline the test function, and then "optimizes" it by moving the alloca
// stack allocation to the beginning of main() and merging it with main()'s
// local variable allocation. This is specially inconvenient for TestAllocaBig*
// functions below, because they make an allocation big enough to kill the
// process, and with inlining they get to kill the process every time.
//
// This workaround makes sure the stack allocation is only done when the test
// function is actually called.

__attribute__((noinline)) int TestAllocaSmallUnderLimit() {
  void* ptr = alloca(1ULL << 20 /* 1 MiB */);
  printf("alloca worked (ptr=%p)\n", ptr);
  return EXIT_SUCCESS;
}

__attribute__((noinline)) int TestAllocaBigUnderLimit() {
  void* ptr = alloca(8ULL << 20 /* 8 MiB */);
  printf("We should have been killed by now (ptr=%p)\n", ptr);
  return EXIT_FAILURE;
}

__attribute__((noinline)) int TestAllocaBigAboveLimit() {
  void* ptr = alloca(100ULL << 20 /* 100 MiB */);
  printf("We should have been killed by now (ptr=%p)\n", ptr);
  return EXIT_FAILURE;
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
      return TestMmapUnderLimit();
    case 2:
      return TestMmapAboveLimit();
    case 3:
      return TestAllocaSmallUnderLimit();
    case 4:
      return TestAllocaBigUnderLimit();
    case 5:
      return TestAllocaBigAboveLimit();
    default:
      printf("Unknown test: %d\n", testno);
      return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
