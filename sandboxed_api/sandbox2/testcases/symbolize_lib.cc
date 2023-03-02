#include "sandboxed_api/sandbox2/testcases/symbolize_lib.h"

#include "absl/base/attributes.h"

ABSL_ATTRIBUTE_NOINLINE
ABSL_ATTRIBUTE_NO_TAIL_CALL
void LibRecurseA(void (*cb)(int), int data, int n);

ABSL_ATTRIBUTE_NOINLINE
ABSL_ATTRIBUTE_NO_TAIL_CALL
void LibRecurseB(void (*cb)(int), int data, int n) {
  if (n > 1) {
    return LibRecurseA(cb, data, n - 1);
  }
  return cb(data);
}

void LibRecurseA(void (*cb)(int), int data, int n) {
  if (n > 1) {
    return LibRecurseB(cb, data, n - 1);
  }
  return cb(data);
}

void LibRecurse(void (*cb)(int), int data, int n) { LibRecurseA(cb, data, n); }
