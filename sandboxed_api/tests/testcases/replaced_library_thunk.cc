#include "sandboxed_api/tests/testcases/replaced_library_thunk.h"

#include <cstddef>

extern "C" {

int sum_buffs(OneOrTwoBuffs* buff) {
  int sum = 0;
  if (buff->kind < kTwo) {
    for (size_t i = 0; i < buff->one_buff.size; ++i) {
      sum += buff->one_buff.buff[i];
    }
    // Extra little boost for kOneA vs kOne.
    if (buff->kind == kOneA) {
      sum += 10;
    }
  } else {
    for (size_t i = 0; i < buff->two_buffs.size1; ++i) {
      sum += buff->two_buffs.buff1[i];
    }
    for (size_t i = 0; i < buff->two_buffs.size2; ++i) {
      sum += buff->two_buffs.buff2[i];
    }
  }
  return sum;
}

}  // extern "C"
