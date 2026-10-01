#include "sandboxed_api/sandbox2/testcases/symbolize_lib.h"

#include "absl/base/attributes.h"

ABSL_ATTRIBUTE_NOINLINE
ABSL_ATTRIBUTE_NO_TAIL_CALL
void LibRecurseA(void (*cb)(int), int data, int n);

ABSL_ATTRIBUTE_NOINLINE
ABSL_ATTRIBUTE_NO_TAIL_CALL
void LibCallCallback(void (*cb)(int), int data) {
  // Allocate > 0x4000 bytes on the stack so that libunwind's x86_64 heuristic
  // frame-pointer fallback in Gstep.c ((rbp - cfa) <= 0x4000) gives up and
  // returns 0, forcing RunLibUnwind() to fall back to
  // UnwindUsingFramePointer().
  char buffer[0x5000];
  asm volatile("" : "+m"(buffer));
  cb(data);
}

ABSL_ATTRIBUTE_NOINLINE
ABSL_ATTRIBUTE_NO_TAIL_CALL
void LibRecurseB(void (*cb)(int), int data, int n) {
  if (n > 1) {
    return LibRecurseA(cb, data, n - 1);
  }
  return LibCallCallback(cb, data);
}

void LibRecurseA(void (*cb)(int), int data, int n) {
  if (n > 1) {
    return LibRecurseB(cb, data, n - 1);
  }
  return LibCallCallback(cb, data);
}

void LibRecurse(void (*cb)(int), int data, int n) { LibRecurseA(cb, data, n); }
