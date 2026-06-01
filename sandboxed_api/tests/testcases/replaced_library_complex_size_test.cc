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

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/types/span.h"

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

TEST(Test, CopyWithInPtrLen) {
  int src_buf[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  int dst_buf[10] = {0};
  size_t len = 6;
  mylib_copy_with_inptr_len(dst_buf, src_buf, &len);
  EXPECT_THAT(dst_buf, testing::ElementsAre(0, 1, 2, 3, 4, 5, 0, 0, 0, 0));

  len = 10;
  mylib_copy_with_inptr_len(dst_buf, src_buf, &len);
  EXPECT_THAT(dst_buf, testing::ElementsAre(0, 1, 2, 3, 4, 5, 6, 7, 8, 9));
}

static constexpr int kZero[] = {0, 0, 0, 0, 0};
static constexpr int kOne[] = {1, 1, 1};
static constexpr int kUnknown[] = {9, 9, 9, 9, 9, 9, 9};

TEST(Test, SetWithOutPtrLen) {
  int dst_buf[20];
  size_t len = 0;
  size_t capacity = sizeof(dst_buf);

  mylib_set_with_outptr_len_capacity(0, dst_buf, capacity, &len);
  EXPECT_EQ(len, std::size(kZero));
  EXPECT_THAT(absl::MakeSpan(dst_buf, len), testing::ElementsAreArray(kZero));

  mylib_set_with_outptr_len_capacity(1, dst_buf, capacity, &len);
  EXPECT_EQ(len, std::size(kOne));
  EXPECT_THAT(absl::MakeSpan(dst_buf, len), testing::ElementsAreArray(kOne));

  mylib_set_with_outptr_len_capacity(5, dst_buf, capacity, &len);
  EXPECT_EQ(len, std::size(kUnknown));
  EXPECT_THAT(absl::MakeSpan(dst_buf, len),
              testing::ElementsAreArray(kUnknown));
}

TEST(Test, SetWithOutPtrBytes) {
  int dst_buf[20];
  size_t num_bytes = 0;
  size_t capacity = sizeof(dst_buf);

  mylib_set_with_outptr_bytes_capacity(0, dst_buf, capacity, &num_bytes);
  EXPECT_EQ(num_bytes, sizeof(kZero));
  EXPECT_THAT(absl::MakeSpan(dst_buf, num_bytes / sizeof(int)),
              testing::ElementsAreArray(kZero));

  mylib_set_with_outptr_bytes_capacity(1, dst_buf, capacity, &num_bytes);
  EXPECT_EQ(num_bytes, sizeof(kOne));
  EXPECT_THAT(absl::MakeSpan(dst_buf, num_bytes / sizeof(int)),
              testing::ElementsAreArray(kOne));

  mylib_set_with_outptr_bytes_capacity(5, dst_buf, capacity, &num_bytes);
  EXPECT_EQ(num_bytes, sizeof(kUnknown));
  EXPECT_THAT(absl::MakeSpan(dst_buf, num_bytes / sizeof(int)),
              testing::ElementsAreArray(kUnknown));
}

TEST(Test, NextStringInOutPtrLen) {
  int src_dst_buf[20];
  memcpy(src_dst_buf, kZero, sizeof(kZero));

  size_t len = std::size(kZero);
  size_t capacity = sizeof(src_dst_buf);
  mylib_set_with_inoutptr_len(src_dst_buf, capacity, &len);
  EXPECT_EQ(len, std::size(kOne));
  EXPECT_THAT(absl::MakeSpan(src_dst_buf, len),
              testing::ElementsAreArray(kOne));

  mylib_set_with_inoutptr_len(src_dst_buf, capacity, &len);
  EXPECT_EQ(len, std::size(kUnknown));
  EXPECT_THAT(absl::MakeSpan(src_dst_buf, len),
              testing::ElementsAreArray(kUnknown));
}

}  // namespace
