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

#include "sandboxed_api/tests/testcases/replaced_library_multi_level_ptrs.h"

#include <stddef.h>

#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace {

TEST(MultiLevelPointersTest, TwoLevelOpaqueHandleOutAndInOut) {
  DbHandle* db = nullptr;
  ASSERT_EQ(open_db_handle(42, &db), 0);
  ASSERT_NE(db, nullptr);
  EXPECT_EQ(read_db_handle(db), 42);

  // Update in place via `DbHandle**` (`SANDBOX_INOUT_PTR`).
  EXPECT_EQ(update_or_close_db_handle(&db, 99), 42);
  ASSERT_NE(db, nullptr);
  EXPECT_EQ(read_db_handle(db), 99);

  // Close and null out `*db` via `DbHandle**` (`new_value < 0`).
  EXPECT_EQ(update_or_close_db_handle(&db, -1), 99);
  EXPECT_EQ(db, nullptr);

  // Null outer pointer is preserved across the sandbox boundary.
  EXPECT_EQ(open_db_handle(10, nullptr), -1);
}

TEST(MultiLevelPointersTest,
     TwoLevelSharedCopyFromAndBindTypedefKeepsSlotsDistinct) {
  DbHandle* db = nullptr;
  ASSERT_EQ(open_db_handle(100, &db), 0);
  ASSERT_NE(db, nullptr);

  const int* header = nullptr;
  const int* footer = nullptr;
  const int* alt = nullptr;
  ASSERT_EQ(get_db_header_and_footer(db, &header, &footer), 0);
  ASSERT_EQ(get_db_alt_slice(db, &alt), 0);
  ASSERT_NE(header, nullptr);
  ASSERT_NE(footer, nullptr);
  ASSERT_NE(alt, nullptr);
  EXPECT_NE(header, footer);
  EXPECT_NE(header, alt);
  EXPECT_NE(footer, alt);
  EXPECT_EQ(header[0], 101);
  EXPECT_EQ(header[1], 102);
  EXPECT_EQ(footer[0], 110);
  EXPECT_EQ(footer[1], 120);
  EXPECT_EQ(alt[0], 200);
  EXPECT_EQ(alt[1], 300);

  close_db_handle(db);
}

TEST(MultiLevelPointersTest, TwoLevelCStringArrayAndStaticStringOut) {
  const char* words[] = {"hello", nullptr, "sandbox", "!"};
  const char* status = nullptr;

  EXPECT_EQ(sum_string_lengths(4, words, &status), 13u);
  ASSERT_NE(status, nullptr);
  EXPECT_EQ(std::string(status), "ok");

  const char* empty_words[] = {nullptr, ""};
  EXPECT_EQ(sum_string_lengths(2, empty_words, &status), 0u);
  ASSERT_NE(status, nullptr);
  EXPECT_EQ(std::string(status), "empty");

  const char* expected = "empty";
  EXPECT_EQ(match_or_replace_status(&expected, &status), 1);
  ASSERT_NE(status, nullptr);
  EXPECT_EQ(std::string(status), "matched");

  EXPECT_EQ(match_or_replace_status(&expected, &status), 0);
  ASSERT_NE(status, nullptr);
  EXPECT_EQ(std::string(status), "mismatch");
}

TEST(MultiLevelPointersTest, TwoLevel2DGridInOut) {
  std::vector<int> row0 = {1, 2, 3};
  std::vector<int> row2 = {4, 5, 6};
  // Include a null row in the middle to verify null inner pointers are handled
  // gracefully.
  int* grid[] = {row0.data(), nullptr, row2.data()};

  EXPECT_EQ(scale_2d_grid(grid, /*rows=*/3, /*cols=*/3, /*factor=*/10), 210);
  EXPECT_EQ(row0, (std::vector<int>{10, 20, 30}));
  EXPECT_EQ(row2, (std::vector<int>{40, 50, 60}));
}

TEST(MultiLevelPointersTest, ThreeLevelMatrixHandleInOut) {
  std::vector<int> row0 = {10, 20};
  std::vector<int> row1 = {30, 40};
  int* rows[] = {row0.data(), row1.data()};
  int** matrix = rows;

  // Pass `&matrix` (`int***`) across the sandbox boundary.
  EXPECT_EQ(add_to_matrix_via_3level_ptr(&matrix, /*rows=*/2, /*cols=*/2,
                                         /*delta=*/5),
            120);
  EXPECT_EQ(row0, (std::vector<int>{15, 25}));
  EXPECT_EQ(row1, (std::vector<int>{35, 45}));

  EXPECT_EQ(add_to_matrix_via_3level_ptr(nullptr, 2, 2, 5), 0);
}

TEST(MultiLevelPointersTest, ThreeLevel3DTensorInOut) {
  // 2 layers x 2 rows x 3 cols, with one null row to exercise sparse 3D
  // structures.
  std::vector<int> l0_r0 = {1, 2, 3};
  std::vector<int> l0_r1 = {4, 5, 6};
  int* layer0[] = {l0_r0.data(), l0_r1.data()};

  std::vector<int> l1_r1 = {7, 8, 9};
  int* layer1[] = {nullptr, l1_r1.data()};

  int** tensor[] = {layer0, layer1};

  // Initial sum = 1+2+3+4+5+6+7+8+9 = 45.
  // 3 non-null rows * 3 cols = 9 elements incremented by 2 -> 45 + 18 = 63.
  EXPECT_EQ(increment_3d_tensor(tensor, /*layers=*/2, /*rows=*/2, /*cols=*/3,
                                /*delta=*/2),
            63);
  EXPECT_EQ(l0_r0, (std::vector<int>{3, 4, 5}));
  EXPECT_EQ(l0_r1, (std::vector<int>{6, 7, 8}));
  EXPECT_EQ(l1_r1, (std::vector<int>{9, 10, 11}));
}

TEST(MultiLevelPointersTest, ThreeLevelOpaqueHandleBatchOut) {
  DbHandle* handles[3] = {nullptr, nullptr, nullptr};
  DbHandle** handle_list = handles;

  ASSERT_EQ(open_db_batch_via_3level_ptr(3, /*base_value=*/100, &handle_list),
            3);
  for (int i = 0; i < 3; ++i) {
    ASSERT_NE(handles[i], nullptr);
    EXPECT_EQ(read_db_handle(handles[i]), 100 + i);
    close_db_handle(handles[i]);
  }
}

}  // namespace
