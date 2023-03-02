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

// A binary that exits via different modes: crashes, causes violation, exits
// normally or times out, to test the stack tracing symbolizer.

#include <sys/syscall.h>
#include <unistd.h>

#include <cstdlib>

#include "absl/base/attributes.h"
#include "absl/strings/numbers.h"
#include "sandboxed_api/util/raw_logging.h"

// Sometimes we don't have debug info to properly unwind through libc (a frame
// is skipped).
// Workaround by putting another frame on the call stack.
template <typename F>
ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL void IndirectLibcCall(
    F func) {
  func();
}

ABSL_ATTRIBUTE_NOINLINE
void CrashMe(char x = 0) {
  volatile char* null = nullptr;
  *null = x;
}

ABSL_ATTRIBUTE_NOINLINE
ABSL_ATTRIBUTE_NO_TAIL_CALL
void ViolatePolicy(int x = 0) {
  IndirectLibcCall([x]() { syscall(__NR_ptrace, x); });
}

ABSL_ATTRIBUTE_NOINLINE
ABSL_ATTRIBUTE_NO_TAIL_CALL
void ExitNormally(int x = 0) {
  IndirectLibcCall([x]() {
    // _exit is marked noreturn, which makes stack traces a bit trickier -
    // work around by using a volatile read
    if (volatile int y = 1) {
      _exit(x);
    }
  });
}

ABSL_ATTRIBUTE_NOINLINE
ABSL_ATTRIBUTE_NO_TAIL_CALL
void SleepForXSeconds(int x = 0) {
  IndirectLibcCall([x]() { sleep(x); });
}

int main(int argc, char* argv[]) {
  SAPI_RAW_CHECK(argc >= 2, "Not enough arguments");
  int testno;
  SAPI_RAW_CHECK(absl::SimpleAtoi(argv[1], &testno), "testno not a number");
  switch (testno) {
    case 1:
      CrashMe();
      break;
    case 2:
      ViolatePolicy();
      break;
    case 3:
      ExitNormally();
      break;
    case 4:
      SleepForXSeconds(10);
      break;
    default:
      SAPI_RAW_LOG(FATAL, "Unknown test case: %d", testno);
  }
  return EXIT_SUCCESS;
}
