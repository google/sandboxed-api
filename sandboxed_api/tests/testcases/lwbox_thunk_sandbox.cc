#include <cassert>
#include <cstddef>

#include "sandboxed_api/annotations.h"
#include "sandboxed_api/tests/testcases/replaced_library_thunk.h"

extern "C" {

SANDBOX_FUNCS(sum_buffs, sum_buffs_sandbox);

// An example thunk for deeply syncing pointers in a struct with a union.
// Otherwise, we need some annotation capturing a mapping of predicates
// to the member of the union that is active. Then use that mapping to
// set up some branches to decide which active member to sync.
//
// Here, the thunk does the branching and flattens the active member of the
// union in the struct into parameters, then re-assembles it back into the
// active member of the struct in the sandbox.
int sum_buffs(OneOrTwoBuffs* buff SANDBOX_OPAQUE_PTR);

SANDBOX_HOST_CODE(R"cc(
#include "sandboxed_api/tests/testcases/replaced_library_thunk.h"
)cc");

SANDBOX_SANDBOXEE_THUNK(sum_buffs)
int sum_buffs_sandbox(
    int kind, int* one_buff SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(one_size),
    size_t one_size,
    int* two_buff1 SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(two_size1),
    size_t two_size1,
    int* two_buff2 SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(two_size2),
    size_t two_size2) {
  OneOrTwoBuffs buff;
  if (kind < kTwo) {
    buff.kind = static_cast<Kind>(kind);
    buff.one_buff.buff = one_buff;
    buff.one_buff.size = one_size;
  } else {
    buff.kind = static_cast<Kind>(kind);
    buff.two_buffs.buff1 = two_buff1;
    buff.two_buffs.size1 = two_size1;
    buff.two_buffs.buff2 = two_buff2;
    buff.two_buffs.size2 = two_size2;
  }
  return sum_buffs(&buff);
}

SANDBOX_HOST_THUNK(sum_buffs)
int sum_buffs_host(OneOrTwoBuffs* buff) {
  // Split the union into the individual buffers with sizes
  if (buff->kind < kTwo) {
    return sum_buffs_sandbox(static_cast<int>(buff->kind), buff->one_buff.buff,
                             buff->one_buff.size, nullptr, 0, nullptr, 0);
  } else {
    assert(buff->kind == kTwo);
    return sum_buffs_sandbox(static_cast<int>(buff->kind), nullptr, 0,
                             buff->two_buffs.buff1, buff->two_buffs.size1,
                             buff->two_buffs.buff2, buff->two_buffs.size2);
  }
}

}  // extern "C"
