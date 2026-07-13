#include "sandboxed_api/tests/testcases/replaced_library_thunk.h"

#include <string>

#include "gtest/gtest.h"

namespace {

TEST(Test, UsesThunkForUnion) {
  OneOrTwoBuffs buff;
  // case kOne
  buff.kind = kOne;
  buff.one_buff.size = 5;
  buff.one_buff.buff = new int[5]{1, 2, 3, 4, 5};
  EXPECT_EQ(sum_buffs(&buff), 15);

  // case kOneA
  buff.kind = kOneA;
  EXPECT_EQ(sum_buffs(&buff), 25);
  delete[] buff.one_buff.buff;

  // case kTwo
  buff.kind = kTwo;
  buff.two_buffs.size1 = 3;
  buff.two_buffs.buff1 = new int[3]{1, 2, 3};
  buff.two_buffs.size2 = 3;
  buff.two_buffs.buff2 = new int[3]{4, 5, 6};
  EXPECT_EQ(sum_buffs(&buff), 21);
  delete[] buff.two_buffs.buff1;
  delete[] buff.two_buffs.buff2;
}

}  // namespace
