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

struct SaxCaptureState {
  std::string tag;
  bool atts_is_null = false;
  std::vector<std::string> attrs;
};

void RecordSaxStartElement(void* ctx, const sax_xml_char_t* name,
                           const sax_xml_char_t** atts) {
  auto* state = static_cast<SaxCaptureState*>(ctx);
  if (name != nullptr) {
    state->tag = reinterpret_cast<const char*>(name);
  }
  state->atts_is_null = (atts == nullptr);
  if (atts != nullptr) {
    for (size_t i = 0; atts[i] != nullptr; ++i) {
      state->attrs.emplace_back(reinterpret_cast<const char*>(atts[i]));
    }
  }
}

TEST(MultiLevelPointersTest,
     NullTerminatedPointerArraysSupportLuaCheckOptionAndSaxAttributes) {
  // 1. Lua `luaL_checkoption` pattern: `const char* const lst[]` terminated by
  //    `nullptr`.
  const char* const kModes[] = {"none", "line", "full", nullptr};
  EXPECT_EQ(check_option_from_list("line", "none", kModes), 1);
  EXPECT_EQ(check_option_from_list("full", "none", kModes), 2);
  // Falls back to `def` when `name` is empty.
  EXPECT_EQ(check_option_from_list("", "none", kModes), 0);
  EXPECT_EQ(check_option_from_list("unknown", "none", kModes), -1);
  EXPECT_EQ(check_option_from_list("line", "none", nullptr), -1);

  // 2. libxml2/expat `startElementSAXFunc` pattern: `const xmlChar** atts`
  //    terminated by `nullptr`, passed from host -> sandbox -> host callback.
  const sax_xml_char_t kItemTag[] = {'i', 't', 'e', 'm', 0};
  const sax_xml_char_t kAttrId[] = {'i', 'd', 0};
  const sax_xml_char_t kVal42[] = {'4', '2', 0};
  const sax_xml_char_t kAttrLang[] = {'l', 'a', 'n', 'g', 0};
  const sax_xml_char_t kValEn[] = {'e', 'n', 0};
  const sax_xml_char_t* atts[] = {kAttrId, kVal42, kAttrLang, kValEn, nullptr};

  SaxCaptureState captured;
  dispatch_sax_start_element(&captured, kItemTag, atts, &RecordSaxStartElement);
  EXPECT_EQ(captured.tag, "item");
  EXPECT_EQ(captured.attrs,
            (std::vector<std::string>{"id", "42", "lang", "en"}));

  // A null `atts` stays null across both boundaries.
  SaxCaptureState empty_captured;
  dispatch_sax_start_element(&empty_captured, kItemTag, nullptr,
                             &RecordSaxStartElement);
  EXPECT_EQ(empty_captured.tag, "item");
  EXPECT_TRUE(empty_captured.atts_is_null);

  // 3. Reverse callback: sandboxee returns `default_sax_start_element`, and the
  //    host invokes it directly with a `nullptr`-terminated attribute array.
  reset_default_sax_attr_byte_sum();
  sax_start_element_cb default_cb = get_default_sax_start_element_cb();
  ASSERT_NE(default_cb, nullptr);
  default_cb(nullptr, kItemTag, atts);
  int expected_sum = ('i' + 't' + 'e' + 'm') + ('i' + 'd') + ('4' + '2') +
                     ('l' + 'a' + 'n' + 'g') + ('e' + 'n');
  EXPECT_EQ(get_default_sax_attr_byte_sum(), expected_sum);

  // The host can also pass a null `name` or `atts`. Only the non-null argument
  // adds to the sum.
  constexpr int kTagSum = 'i' + 't' + 'e' + 'm';
  reset_default_sax_attr_byte_sum();
  default_cb(nullptr, nullptr, atts);
  EXPECT_EQ(get_default_sax_attr_byte_sum(), expected_sum - kTagSum);
  reset_default_sax_attr_byte_sum();
  default_cb(nullptr, kItemTag, nullptr);
  EXPECT_EQ(get_default_sax_attr_byte_sum(), kTagSum);
  reset_default_sax_attr_byte_sum();
  EXPECT_EQ(get_default_sax_attr_byte_sum(), 0);
}

}  // namespace
