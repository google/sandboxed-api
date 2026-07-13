#ifndef SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_THUNK_H_
#define SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_THUNK_H_

#include <cstddef>

extern "C" {

enum Kind {
  kOne,
  kOneA,  // extra version of kOne to make the mapping of kind -> union member
          // more complicated. A real example is the WebP colorspace enum
          // where you group the various RGB-like colorspaces in one branch,
          // and the YUV-like colorspaces in another (still somewhat
          // simple in that there are only two union members, but there can
          // be more in other examples).
  kTwo,
};

struct OneBuff {
  int* buff;
  size_t size;
};

struct TwoBuffs {
  int* buff1;
  int* buff2;
  size_t size1;
  size_t size2;
};

struct OneOrTwoBuffs {
  enum Kind kind;
  union {
    OneBuff one_buff;
    TwoBuffs two_buffs;
  };
};

int sum_buffs(OneOrTwoBuffs* buff);

}  // extern "C"

#endif  // SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_THUNK_H_
