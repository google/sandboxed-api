// Copyright 2026 Google LLC
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

#include "sandboxed_api/tests/testcases/replaced_library_complex_size.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

TEST(Test, CopyWithElemSizedByWithArithmetic) {
  char src_buf[19] = {0, 0,  1,  2,  3,  4,  5,  6,  7, 8,
                      9, 10, 11, 12, 13, 14, 15, 16, 17};
  char dst_buf[19] = {0};
  mylib_copy_image(src_buf, dst_buf, 3, 3);
  EXPECT_THAT(dst_buf, testing::ElementsAre(0xBC, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                            10, 11, 12, 13, 14, 15, 16, 17));
}

TEST(Test, FillBytes) {
  char dst_buf[10] = {0};
  mylib_fill_bytes(dst_buf, 0xAA, 10);
  EXPECT_THAT(dst_buf, testing::ElementsAre(0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
                                            0xAA, 0xAA, 0xAA, 0xAA));
}

}  // namespace
