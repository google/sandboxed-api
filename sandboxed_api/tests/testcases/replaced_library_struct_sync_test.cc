#include "sandboxed_api/tests/testcases/replaced_library_struct_sync.h"

#include "gtest/gtest.h"

namespace {

TEST(StructSyncTest, MungeMixedStruct) {
  MixedStruct mixed;
  InitMixedStruct(&mixed);

  EXPECT_EQ(MungeMixedStruct(&mixed), 123 + 0x12345678);

  EXPECT_EQ(MungeMixedStruct(&mixed), 123 + (2 * 0x12345678));
}

}  // namespace
