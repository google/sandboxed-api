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
#include <stdint.h>
#include <stdlib.h>

#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace {

bool IsSandboxed() {
  int probe = 123;
  return is_marshaled_copy(&probe, reinterpret_cast<uintptr_t>(&probe)) != 0;
}

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

TEST(MultiLevelPointersTest,
     NonConstAndInPlaceMutableNullTerminatedStringsRestoreZeroSentinel) {
  char buf[] = "hello";
  unsigned char ubuf[] = {'w', 'o', 'r', 'l', 'd', 0};
  EXPECT_EQ(measure_non_const_c_strings(buf, ubuf), 10u);

  uppercase_and_clobber_terminator(buf, /*clobber_terminator=*/0);
  EXPECT_EQ(std::string(buf), "HELLO");

  // Under the sandbox, if the sandboxee overwrites the trailing NUL byte in an
  // in-place `SANDBOX_INOUT_PTR SANDBOX_NULL_TERMINATED` buffer, the host
  // restores the trailing zero sentinel after copying the buffer back.
  if (IsSandboxed()) {
    char mutable_buf[] = "abc";
    uppercase_and_clobber_terminator(mutable_buf, /*clobber_terminator=*/1);
    ASSERT_EQ(mutable_buf[3], '\0');
    EXPECT_EQ(std::string(mutable_buf), "ABC");
  }
}

TEST(MultiLevelPointersTest,
     SingleStructPointerInfersStructSyncFromAnnotateStruct) {
  std::vector<int> values = {2, 4, 6};
  TaggedIntSlice slice{
      .label = "items",
      .elems = values.data(),
      .count = values.size(),
  };

  // strlen("items") == 5, plus (2*3 + 4*3 + 6*3) == 36 -> 41.
  EXPECT_EQ(sum_and_scale_tagged_slice(&slice, /*factor=*/3), 41);
  EXPECT_EQ(values, (std::vector<int>{6, 12, 18}));
}

TEST(MultiLevelPointersTest, InOutCountCapacityUsesPreCallCapacity) {
  std::vector<int> dst(8, -1);
  size_t count = dst.size();
  ASSERT_EQ(fill_up_to_inout_count(dst.data(), &count, /*actual_count=*/3,
                                   /*base_value=*/50),
            0);
  EXPECT_EQ(count, 3u);
  EXPECT_EQ(dst, (std::vector<int>{50, 51, 52, -1, -1, -1, -1, -1}));
  EXPECT_EQ(fill_up_to_inout_count(nullptr, nullptr, /*actual_count=*/3,
                                   /*base_value=*/50),
            -1);
  EXPECT_EQ(fill_up_to_inout_count(dst.data(), nullptr, /*actual_count=*/3,
                                   /*base_value=*/50),
            -1);

  if (IsSandboxed()) {
    // If the sandboxee overwrites `*inout_count` with a size larger than the
    // pre-call capacity, the host checks against the pre-call capacity stored
    // in `sapi_tmp_dst.GetSize()` rather than re-reading the overwritten
    // `*inout_count`.
    EXPECT_DEATH(
        {
          std::vector<int> small_dst(3, 0);
          size_t small_cap = small_dst.size();
          fill_up_to_inout_count(small_dst.data(), &small_cap,
                                 /*actual_count=*/6, /*base_value=*/1);
        },
        "Sandboxee returned size \\(24\\) exceeding capacity \\(12\\)");
  }
}

TEST(MultiLevelPointersTest, RetainedInOutBufferClampsPostCallSizeToCapacity) {
  DbHandle* db = nullptr;
  ASSERT_EQ(open_db_handle(7, &db), 0);
  ASSERT_NE(db, nullptr);

  std::vector<int> buf = {10, 20, 30, 40, 900, 901};
  size_t count = 4;
  ASSERT_EQ(retain_inout_db_buffer(db, buf.data(), &count, /*actual_count=*/2,
                                   /*base_value=*/100),
            0);
  EXPECT_EQ(count, 2u);
  // The sandboxee updates the first 2 elements in place. Elements past the
  // pre-call capacity of 4 are never touched.
  EXPECT_EQ(buf, (std::vector<int>{110, 121, 30, 40, 900, 901}));
  close_db_handle(db);

  // A zero post-call count is accepted and written back.
  DbHandle* db_zero = nullptr;
  ASSERT_EQ(open_db_handle(8, &db_zero), 0);
  size_t zero_cap = 2;
  ASSERT_EQ(retain_inout_db_buffer(db_zero, buf.data(), &zero_cap,
                                   /*actual_count=*/0, /*base_value=*/5),
            0);
  EXPECT_EQ(zero_cap, 0u);
  close_db_handle(db_zero);

  if (IsSandboxed()) {
    EXPECT_DEATH(([] {
                   DbHandle* bad_db = nullptr;
                   ASSERT_EQ(open_db_handle(9, &bad_db), 0);
                   std::vector<int> small_buf = {1, 2, 3};
                   size_t small_cap = small_buf.size();
                   retain_inout_db_buffer(bad_db, small_buf.data(), &small_cap,
                                          /*actual_count=*/6, /*base_value=*/1);
                 }()),
                 "sapi_post_size_buf <= sapi_cap_buf");
  }
}

TEST(MultiLevelPointersTest,
     OutparamSizedBufferPreservesInitialOutCountOnEarlyReturnAndZeroCapacity) {
  std::vector<int> dst(4, -7);
  size_t out_count = 0;

  // Populate and free a call first so sandboxee heap chunks are non-zero, then
  // trigger an early error return that leaves `*out_count` unwritten.
  ASSERT_EQ(fill_or_fail_outparam_sized(dst.data(), dst.size() * sizeof(int),
                                        &out_count, /*should_fail=*/0,
                                        /*write_count=*/3, /*fill_value=*/10),
            0);
  EXPECT_EQ(out_count, 3u);
  EXPECT_EQ(dst, (std::vector<int>{10, 11, 12, -7}));

  out_count = 0;
  std::vector<int> untouched(4, 55);
  EXPECT_EQ(fill_or_fail_outparam_sized(
                untouched.data(), untouched.size() * sizeof(int), &out_count,
                /*should_fail=*/1, /*write_count=*/999, /*fill_value=*/1),
            -1);
  EXPECT_EQ(out_count, 0u);
  EXPECT_EQ(untouched, (std::vector<int>{55, 55, 55, 55}));

  // A zero-byte capacity with a zero-byte result succeeds and leaves `dst`
  // untouched.
  int sentinel = 4242;
  out_count = 3;
  ASSERT_EQ(fill_or_fail_outparam_sized(&sentinel, /*capacity_bytes=*/0,
                                        &out_count, /*should_fail=*/0,
                                        /*write_count=*/0, /*fill_value=*/9),
            0);
  EXPECT_EQ(out_count, 0u);
  EXPECT_EQ(sentinel, 4242);
}

TEST(MultiLevelPointersTest,
     RetainedBufferWithNullOrSignedInOutCountGuardsDereferenceAndNegativeSize) {
  std::vector<int> buf = {5, 6, 7};
  // Null `inout_count` with a non-null `buf` must not dereference `inout_count`
  // on the host.
  DbHandle* db_null1 = nullptr;
  ASSERT_EQ(open_db_handle(10, &db_null1), 0);
  EXPECT_EQ(retain_inout_db_buffer(db_null1, buf.data(),
                                   /*inout_count=*/nullptr,
                                   /*actual_count=*/2, /*base_value=*/10),
            -1);
  close_db_handle(db_null1);

  DbHandle* db_null2 = nullptr;
  ASSERT_EQ(open_db_handle(11, &db_null2), 0);
  EXPECT_EQ(retain_signed_inout_db_buffer(db_null2, buf.data(),
                                          /*inout_count=*/nullptr,
                                          /*actual_count=*/2,
                                          /*base_value=*/10),
            -1);
  close_db_handle(db_null2);

  DbHandle* db = nullptr;
  ASSERT_EQ(open_db_handle(12, &db), 0);
  ASSERT_NE(db, nullptr);

  int count = 2;
  ASSERT_EQ(retain_signed_inout_db_buffer(db, buf.data(), &count,
                                          /*actual_count=*/2,
                                          /*base_value=*/100),
            0);
  EXPECT_EQ(count, 2);
  EXPECT_EQ(buf, (std::vector<int>{105, 107, 7}));
  close_db_handle(db);

  if (IsSandboxed()) {
    // Negative pre-call signed `*inout_count` traps on the host via
    // `CheckedToSizeT`.
    EXPECT_DEATH(([] {
                   DbHandle* neg_db = nullptr;
                   ASSERT_EQ(open_db_handle(12, &neg_db), 0);
                   std::vector<int> tmp = {1, 2};
                   int neg_count = -2;
                   retain_signed_inout_db_buffer(neg_db, tmp.data(), &neg_count,
                                                 /*actual_count=*/0,
                                                 /*base_value=*/1);
                 }()),
                 "Negative buffer size");

    // Negative post-call signed `*inout_count` written by the sandboxee traps
    // on the host via `CheckedToSizeT`.
    EXPECT_DEATH(([] {
                   DbHandle* neg_post_db = nullptr;
                   ASSERT_EQ(open_db_handle(13, &neg_post_db), 0);
                   std::vector<int> tmp = {1, 2};
                   int cap_count = 2;
                   retain_signed_inout_db_buffer(neg_post_db, tmp.data(),
                                                 &cap_count,
                                                 /*actual_count=*/-3,
                                                 /*base_value=*/1);
                 }()),
                 "Negative buffer size");
  }
}

TEST(MultiLevelPointersTest,
     InPtrSizedBySignedInOutCountGuardsNullAndSkipsWritebackIntoReadOnlySrc) {
  static constexpr int kReadOnlySrc[] = {10, 20, 30, 40};

  EXPECT_EQ(sum_in_ptr_with_signed_inout_count(nullptr, nullptr, 0), -1);
  EXPECT_EQ(sum_in_ptr_with_signed_inout_count(kReadOnlySrc, nullptr, 0), -1);

  int count = 3;
  // `updated_count` (99) exceeds the pre-call input count (3): because `src` is
  // `SANDBOX_IN_PTR`, `*inout_count` updates to 99 without rejecting or writing
  // back into read-only `kReadOnlySrc`.
  EXPECT_EQ(sum_in_ptr_with_signed_inout_count(kReadOnlySrc, &count,
                                               /*updated_count=*/99),
            60);
  EXPECT_EQ(count, 99);

  if (IsSandboxed()) {
    EXPECT_DEATH(
        {
          int neg_count = -4;
          sum_in_ptr_with_signed_inout_count(kReadOnlySrc, &neg_count, 0);
        },
        "Negative buffer size");
  }
}

int TestOutCountedFillCb(int* dst, size_t cap_bytes, int* out_count,
                         int should_fail) {
  if (should_fail != 0) {
    return -1;
  }
  if (dst == nullptr || out_count == nullptr || cap_bytes < 2 * sizeof(int)) {
    return -2;
  }
  dst[0] = 42;
  dst[1] = 43;
  *out_count = 2;
  return 0;
}

int TestInOutCountedXformCb(int* buf, size_t cap_bytes, int* inout_count,
                            int should_fail) {
  if (buf == nullptr || inout_count == nullptr) {
    return -2;
  }
  if (should_fail != 0) {
    return -1;
  }
  int sum = 0;
  for (int i = 0;
       i < *inout_count && static_cast<size_t>(i) < cap_bytes / sizeof(int);
       ++i) {
    sum += buf[i];
    buf[i] += 1;
  }
  *inout_count = 1;
  return sum;
}

TEST(MultiLevelPointersTest,
     CallbackOutCountSiblingIgnoresSandboxeePreCallGarbageOnEarlyReturn) {
  int first_elem = 0;
  // Even when the sandboxee leaves out-of-range, negative, or in-bounds garbage
  // in its `SANDBOX_OUT_PTR` count slot before calling the host callback, an
  // early return from the host callback must neither abort the host nor
  // overwrite the sandboxee buffer with zeroes.
  for (int garbage : {99999, -5, 2}) {
    first_elem = 0;
    EXPECT_EQ(invoke_out_counted_callback(&TestOutCountedFillCb, garbage,
                                          /*should_fail=*/1, &first_elem),
              -1);
    EXPECT_EQ(first_elem, 777);
  }

  first_elem = 0;
  EXPECT_EQ(invoke_out_counted_callback(&TestOutCountedFillCb,
                                        /*initial_out_count_garbage=*/99999,
                                        /*should_fail=*/0, &first_elem),
            0);
  EXPECT_EQ(first_elem, 42);
}

TEST(MultiLevelPointersTest,
     CallbackInOutCountSiblingValidatesPreCallBoundsAndHandlesEarlyReturn) {
  int final_count = 0;
  EXPECT_EQ(invoke_inout_counted_callback(&TestInOutCountedXformCb,
                                          /*initial_inout_count=*/2,
                                          /*should_fail=*/0, &final_count),
            30);
  EXPECT_EQ(final_count, 1);

  final_count = 0;
  EXPECT_EQ(invoke_inout_counted_callback(&TestInOutCountedXformCb,
                                          /*initial_inout_count=*/2,
                                          /*should_fail=*/1, &final_count),
            -1);
  EXPECT_EQ(final_count, 2);

  if (IsSandboxed()) {
    // The callback accepts a negative count when `should_fail` is 0, so only
    // the pre-call check can abort here.
    EXPECT_DEATH(
        {
          int unused_final = 0;
          invoke_inout_counted_callback(&TestInOutCountedXformCb,
                                        /*initial_inout_count=*/-3,
                                        /*should_fail=*/0, &unused_final);
        },
        "Negative buffer size");
    EXPECT_DEATH(
        {
          int unused_final = 0;
          invoke_inout_counted_callback(&TestInOutCountedXformCb,
                                        /*initial_inout_count=*/99,
                                        /*should_fail=*/1, &unused_final);
        },
        "sapi_in_bytes_buf <= sapi_arr_buf->GetSize\\(\\)");
  }
}

TEST(MultiLevelPointersTest,
     LifetimeMallocTransfersOwnershipForReturnsOutParamsAndStructFields) {
  // 1. Null-terminated `char*` return value (`SANDBOX_LIFETIME_MALLOC`).
  char* rev = duplicate_and_reverse_malloc_string("sandbox");
  ASSERT_NE(rev, nullptr);
  EXPECT_EQ(std::string(rev), "xobdnas");
  free(rev);
  EXPECT_EQ(duplicate_and_reverse_malloc_string(""), nullptr);

  // 2. Sized `int*` return value (`SANDBOX_ELEM_SIZED_BY(*out_count)` +
  //    `SANDBOX_LIFETIME_MALLOC`).
  size_t seq_count = 0;
  int* seq =
      allocate_arithmetic_sequence(4, /*start=*/10, /*step=*/5, &seq_count);
  ASSERT_NE(seq, nullptr);
  EXPECT_EQ(seq_count, 4u);
  EXPECT_EQ(std::vector<int>(seq, seq + seq_count),
            (std::vector<int>{10, 15, 20, 25}));
  free(seq);
  EXPECT_EQ(
      allocate_arithmetic_sequence(0, /*start=*/10, /*step=*/5, &seq_count),
      nullptr);
  EXPECT_EQ(seq_count, 0u);
  // The host must not dereference a null size pointer.
  int* null_count_seq = allocate_arithmetic_sequence(
      4, /*start=*/10, /*step=*/5, /*out_count=*/nullptr);
  ASSERT_NE(null_count_seq, nullptr);
  free(null_count_seq);

  // 3. `char**` (via pointee `typedef`) and direct `int**` out-parameters with
  //    `SANDBOX_LIFETIME_MALLOC`.
  char* tag = nullptr;
  int* buf = nullptr;
  size_t len = 0;
  ASSERT_EQ(allocate_tag_and_buffer_outparams("netcdf", /*count=*/3,
                                              /*base=*/100, &tag, &buf, &len),
            0);
  ASSERT_NE(tag, nullptr);
  EXPECT_EQ(std::string(tag), "netcdf:ok");
  free(tag);
  ASSERT_NE(buf, nullptr);
  EXPECT_EQ(len, 3u);
  EXPECT_EQ(std::vector<int>(buf, buf + len),
            (std::vector<int>{100, 101, 102}));
  free(buf);
  ASSERT_EQ(allocate_tag_and_buffer_outparams("", /*count=*/0,
                                              /*base=*/100, &tag, &buf, &len),
            0);
  EXPECT_EQ(tag, nullptr);
  EXPECT_EQ(buf, nullptr);
  EXPECT_EQ(len, 0u);

  // Early error return with non-null garbage in the caller's pointers. The
  // callee does not write the out-parameters, and in sandboxed mode the wrapper
  // sets the host pointers to nullptr. Whether the sandbox slots are zeroed
  // before the call is not visible here; sandboxed_library_emitter_test.cc
  // checks it.
  char dummy_tag = 'x';
  int dummy_val = 42;
  tag = &dummy_tag;
  buf = &dummy_val;
  len = 99u;
  EXPECT_EQ(allocate_tag_and_buffer_outparams("netcdf", /*count=*/3,
                                              /*base=*/-9999, &tag, &buf, &len),
            -1);
  if (IsSandboxed()) {
    EXPECT_EQ(tag, nullptr);
    EXPECT_EQ(buf, nullptr);
  }

  // 4. Struct pointer field (`MemIoBuffer::memory`) with
  //    `SANDBOX_BYTE_SIZED_BY(size) SANDBOX_LIFETIME_MALLOC` (modeled on
  //    `nc_close_memio`).
  MemIoBuffer memio{};
  ASSERT_EQ(finalize_memio_buffer(/*size=*/5, /*fill_byte=*/0x20, /*flags=*/7,
                                  &memio),
            0);
  EXPECT_EQ(memio.size, 5u);
  EXPECT_EQ(memio.flags, 7);
  ASSERT_NE(memio.memory, nullptr);
  const auto* bytes = static_cast<const unsigned char*>(memio.memory);
  EXPECT_EQ(std::vector<unsigned char>(bytes, bytes + memio.size),
            (std::vector<unsigned char>{0x20, 0x21, 0x22, 0x23, 0x24}));
  free(memio.memory);
  ASSERT_EQ(
      finalize_memio_buffer(/*size=*/0, /*fill_byte=*/0, /*flags=*/3, &memio),
      0);
  EXPECT_EQ(memio.size, 0u);
  EXPECT_EQ(memio.flags, 3);
  EXPECT_EQ(memio.memory, nullptr);

  // Early error return with caller garbage in the `out_ptr` struct. The
  // sandboxed wrapper sends a zeroed struct instead of the caller's data (which
  // can hold host pointers), and the callee does not write it, so the caller
  // gets back zeros.
  memio.size = 123u;
  memio.memory = &dummy_val;
  memio.flags = 99;
  EXPECT_EQ(finalize_memio_buffer(/*size=*/5, /*fill_byte=*/0x10, /*flags=*/-1,
                                  &memio),
            -1);
  if (IsSandboxed()) {
    EXPECT_EQ(memio.size, 0u);
    EXPECT_EQ(memio.flags, 0);
    EXPECT_EQ(memio.memory, nullptr);
  }
}

TEST(MultiLevelPointersTest, ElemSizedByBindingWithCompositeKeyAndProduct) {
  const size_t dims[2] = {2, 3};
  EXPECT_EQ(register_hyperslab_var(/*ncid=*/4, /*varid=*/7, dims, 2), 11);
  std::vector<int> hyperslab_out(6, 0);
  EXPECT_EQ(
      read_hyperslab_var(/*ncid=*/4, /*varid=*/7, hyperslab_out.data(), 100),
      6);
  EXPECT_EQ(hyperslab_out, (std::vector<int>{147, 148, 149, 150, 151, 152}));
}

TEST(MultiLevelPointersTest,
     StructMemberSizedByBindingResolvesSiblingFieldsOverShadowingParams) {
  ASSERT_EQ(bind_matrix_var_cols(/*ncid=*/5, /*varid=*/9, /*cols=*/3), 509);
  const int weights[3] = {10, 20, 30};
  std::vector<int> cells = {1, 2, 3, 4, 5, 6};
  const int* orig_weights_ptr = weights;
  int* orig_cells_ptr = cells.data();
  MatrixSliceRequest slice{
      .ncid = 5,
      .varid = 9,
      .rows = 2,
      .cols = 0,
      .row_weights = weights,
      .out_cells = cells.data(),
  };

  // Pass decoy `ncid = -77` (would CHECK-fail `CompositeKey` if used) and
  // decoy `rows = 0` (would transfer 0 elements if used) to prove struct
  // fields win over same-named function parameters while `$cols` resolves the
  // bound size rather than `slice.cols`.
  EXPECT_EQ(compute_matrix_slice(&slice, /*ncid=*/-77, /*rows=*/0), -71);
  EXPECT_EQ(slice.cols, 3u);
  EXPECT_EQ(slice.row_weights, orig_weights_ptr);
  EXPECT_EQ(slice.out_cells, orig_cells_ptr);
  EXPECT_EQ(cells, (std::vector<int>{70, 81, 92, 83, 104, 125}));

  if (IsSandboxed()) {
    std::vector<int> small_cells(6, 0);
    MatrixSliceRequest bad_slice{
        .ncid = 5,
        .varid = 9,
        .rows = 2,
        .cols = 10,  // Triggers post-call `slice->rows = 10` inflation.
        .row_weights = weights,
        .out_cells = small_cells.data(),
    };
    EXPECT_DEATH(
        compute_matrix_slice(&bad_slice, /*ncid=*/-77, /*rows=*/0),
        "Sandboxee returned size \\(120\\) exceeding capacity \\(24\\)");
  }
}

TEST(MultiLevelPointersTest,
     CallbackTrampolineDoesNotReadPastOwnedStackArguments) {
  auto cb6 = +[](int p1, int p2, int p3, int p4, int p5, int p6) -> int {
    return p1 + p2 + p3 + p4 + p5 + p6;
  };
  auto cb8 =
      +[](int p1, int p2, int p3, int p4, int p5, int p6, int p7,
          int p8) -> int { return p1 + p2 + p3 + p4 + p5 + p6 + p7 + p8; };
  // cb6(10..60) = 210; cb8(1..6, 70, 80) = 171 -> total = 381.
  EXPECT_EQ(evaluate_callbacks_at_guarded_stack_boundary(cb6, cb8), 381);
}

TEST(MultiLevelPointersTest, TenIntCallbackReceivesStackArgumentsInOrder) {
  auto cb = +[](int p1, int p2, int p3, int p4, int p5, int p6, int p7, int p8,
                int p9, int p10) -> int {
    return p1 + 2 * p2 + 3 * p3 + 4 * p4 + 5 * p5 + 6 * p6 + 7 * p7 + 8 * p8 +
           9 * p9 + 10 * p10;
  };
  // Each `p_i` is `100 + i` and has weight `i`, so a missing or swapped
  // argument changes the result: 100 * 55 + (1^2 + ... + 10^2) = 5885.
  EXPECT_EQ(evaluate_ten_int_callback(100, cb), 5885);
}

TEST(MultiLevelPointersTest, TenArgFloatAndDoubleCallback) {
  auto ten_arg_cb = +[](int p1, int p2, int p3, int p4, int p5, int p6, int p7,
                        int p8, float f1, double d1) -> double {
    int int_sum = p1 + p2 + p3 + p4 + p5 + p6 + p7 + p8;
    return static_cast<double>(int_sum) * static_cast<double>(f1) + d1;
  };
  double cb_result =
      evaluate_ten_arg_float_callback(10, 20, 30, 1.5f, 0.25, ten_arg_cb);
  // p1..p8 = 10, 20, 30, 11, 21, 31, 12, 22 -> sum = 157
  // 157 * 1.5 + 0.25 = 235.75
  EXPECT_DOUBLE_EQ(cb_result, 235.75);
}

TEST(MultiLevelPointersTest, NineFloatStackSpillCallbackReturnsFloat) {
  auto nine_float_cb =
      +[](float f1, float f2, float f3, float f4, float f5, float f6, float f7,
          float f8, float f9, int scale) -> float {
    return (f1 + 2 * f2 + 3 * f3 + 4 * f4 + 5 * f5 + 6 * f6 + 7 * f7 + 8 * f8 +
            9 * f9) *
           static_cast<float>(scale);
  };
  // base = 0.5 -> f_i = i + 0.5 with weight i, so a swapped argument changes
  // the result: (1^2 + ... + 9^2) + 0.5 * (1 + ... + 9) = 307.5; * 2 = 615.0f.
  EXPECT_FLOAT_EQ(evaluate_nine_float_callback(0.5f, nine_float_cb), 615.0f);
  EXPECT_FLOAT_EQ(evaluate_nine_float_callback(0.5f, nullptr), -1.0f);
}

TEST(MultiLevelPointersTest,
     SizedStructArrayPreservesSandboxeeClearedNullPointerMember) {
  int slice0[] = {5, 6};
  int slice1[] = {10, 20, 30};
  int* orig_ptr1 = slice1;
  OuterPacketRecord packets[3] = {
      {"drop", {slice0, 2}, 0},
      {"keep", {slice1, 3}, 0},
      {"empty", {nullptr, 0}, 0},
  };

  EXPECT_EQ(filter_or_scale_outer_packets(packets, 3, /*min_len=*/3,
                                          /*scale=*/2),
            1);
  EXPECT_EQ(packets[0].payload.values, nullptr);
  EXPECT_EQ(packets[0].payload.len, 0u);
  EXPECT_EQ(packets[0].checksum, -1);
  // Caller's original buffer for element 0 remains untouched.
  EXPECT_EQ(slice0[0], 5);
  EXPECT_EQ(slice0[1], 6);

  EXPECT_EQ(packets[1].payload.values, orig_ptr1);
  EXPECT_EQ(packets[1].payload.len, 3u);
  EXPECT_EQ(slice1[0], 20);
  EXPECT_EQ(slice1[1], 40);
  EXPECT_EQ(slice1[2], 60);
  EXPECT_EQ(packets[1].checksum, 124);

  EXPECT_EQ(packets[2].payload.values, nullptr);
  EXPECT_EQ(packets[2].payload.len, 0u);
}

TEST(MultiLevelPointersTest,
     ExplicitStructSyncOnSizedArrayMutatesAndRezerosNullTerminatedStrings) {
  char label0[] = {'a', 'b', 'c', '\0', 'Z'};
  char label1[] = {'x', 'y', '\0', 'Q'};
  int vals0[] = {1, 2, 3};
  int vals1[] = {10, 20};
  MutableLabeledSlice slices[2] = {
      {label0, vals0, 3},
      {label1, vals1, 2},
  };

  EXPECT_EQ(mutate_labeled_slices(slices, 2, /*clobber_sentinel=*/0,
                                  /*delta=*/5),
            2);
  EXPECT_STREQ(slices[0].label, "ABC");
  EXPECT_EQ(label0[4], 'Z');
  EXPECT_EQ(vals0[0], 6);
  EXPECT_EQ(vals0[1], 7);
  EXPECT_EQ(vals0[2], 8);
  EXPECT_STREQ(slices[1].label, "XY");
  EXPECT_EQ(label1[3], 'Q');
  EXPECT_EQ(vals1[0], 15);
  EXPECT_EQ(vals1[1], 25);

  if (IsSandboxed()) {
    char clobber0[] = {'h', 'i', '\0', 'W'};
    int cvals0[] = {4};
    MutableLabeledSlice clobbered[1] = {{clobber0, cvals0, 1}};
    EXPECT_EQ(mutate_labeled_slices(clobbered, 1, /*clobber_sentinel=*/1,
                                    /*delta=*/1),
              1);
    EXPECT_EQ(clobber0[0], 'H');
    EXPECT_EQ(clobber0[1], 'I');
    EXPECT_EQ(clobber0[2], '\0');
    EXPECT_EQ(clobber0[3], 'W');
    EXPECT_EQ(cvals0[0], 5);
  }
}

TEST(MultiLevelPointersTest,
     ExplicitStructSyncOnSizedArrayRetainsAndBindsPerElementBuffers) {
  int span0[] = {2, 3, 4};
  int span2[] = {10, 20};
  RetainedSpanSpec specs[3] = {
      {span0, 3, 2},
      {nullptr, 0, 7},
      {span2, 2, 3},
  };

  // A zero-length array stages no element buffers.
  EXPECT_EQ(create_retained_span_table(specs, 0), nullptr);
  // A `nullptr` return frees the staged element buffers instead of retaining
  // them under a null context.
  RetainedSpanSpec rejected[2] = {
      {span0, 3, 1},
      {span2, 2, -1},
  };
  EXPECT_EQ(create_retained_span_table(rejected, 2), nullptr);

  RetainedSpanTable* table = create_retained_span_table(specs, 3);
  ASSERT_NE(table, nullptr);
  if (IsSandboxed()) {
    // Mutating host buffers after `create_retained_span_table` must not affect
    // the sandboxee's retained remote copies.
    span0[0] = -100;
    span2[0] = -200;
  }
  // 2 * (2 + 3 + 4) + 0 + 3 * (10 + 20) = 18 + 90 = 108.
  EXPECT_EQ(eval_retained_span_table(table), 108);
  destroy_retained_span_table(table);
}

TEST(MultiLevelPointersTest, ByteAndBindingSizedStructArraysSyncEveryElement) {
  const size_t dims[] = {2, 1};  // elem_count = 2
  ASSERT_EQ(register_hyperslab_var(/*ncid=*/41, /*varid=*/3, dims, 2), 44);
  std::vector<int> values[6] = {{1}, {2, 3}, {4}, {5, 6}, {7}, {8, 9}};
  InnerPayloadSlice byte_sized[2] = {{values[0].data(), 1},
                                     {values[1].data(), 2}};
  InnerPayloadSlice elem_bound[2] = {{values[2].data(), 1},
                                     {values[3].data(), 2}};
  InnerPayloadSlice byte_bound[2] = {{values[4].data(), 1},
                                     {values[5].data(), 2}};

  // Each array has two elements. A wrong element count skips the second
  // element or reads past the end of the array.
  EXPECT_EQ(scale_payload_slice_arrays(byte_sized, sizeof(byte_sized),
                                       /*ncid=*/41, /*varid=*/3, elem_bound,
                                       byte_bound, /*scale=*/10),
            9);
  EXPECT_EQ(values[0], (std::vector<int>{10}));
  EXPECT_EQ(values[1], (std::vector<int>{20, 30}));
  EXPECT_EQ(values[2], (std::vector<int>{40}));
  EXPECT_EQ(values[3], (std::vector<int>{50, 60}));
  EXPECT_EQ(values[4], (std::vector<int>{70}));
  EXPECT_EQ(values[5], (std::vector<int>{80, 90}));
}

TEST(MultiLevelPointersTest, SizedStructArrayTransfersLifetimeMallocMembers) {
  int dummy = 0;
  // The sandboxee sets a `nullptr` buffer for size 0, which must replace the
  // caller's garbage pointer.
  MemIoBuffer memios[3] = {{.size = 7, .memory = &dummy, .flags = -1}, {}, {}};
  ASSERT_EQ(finalize_memio_buffers(memios, 3, /*fill_byte=*/0x40), 0);
  EXPECT_EQ(memios[0].size, 0u);
  EXPECT_EQ(memios[0].memory, nullptr);
  ASSERT_NE(memios[1].memory, nullptr);
  ASSERT_NE(memios[2].memory, nullptr);
  const auto* bytes1 = static_cast<const unsigned char*>(memios[1].memory);
  const auto* bytes2 = static_cast<const unsigned char*>(memios[2].memory);
  EXPECT_EQ(std::vector<unsigned char>(bytes1, bytes1 + memios[1].size),
            (std::vector<unsigned char>{0x50, 0x51}));
  EXPECT_EQ(std::vector<unsigned char>(bytes2, bytes2 + memios[2].size),
            (std::vector<unsigned char>{0x60, 0x61, 0x62, 0x63}));
  EXPECT_EQ(memios[2].flags, 2);
  free(memios[1].memory);
  free(memios[2].memory);
}

TEST(MultiLevelPointersTest, SizedStructArrayRejectsInflatedMemberLength) {
  std::vector<int> values0 = {1, 2};
  std::vector<int> values1 = {3};
  InnerPayloadSlice slices[2] = {{values0.data(), 2}, {values1.data(), 1}};

  // A smaller length is in the capacity, and the host keeps its buffers.
  inflate_payload_slice_lens(slices, 2, /*new_len=*/1);
  EXPECT_EQ(slices[0].values, values0.data());
  EXPECT_EQ(slices[0].len, 1u);
  EXPECT_EQ(slices[1].values, values1.data());
  EXPECT_EQ(slices[1].len, 1u);

  if (IsSandboxed()) {
    // Each element now has a capacity of 1 `int` (4 bytes).
    EXPECT_DEATH(inflate_payload_slice_lens(slices, 2, /*new_len=*/3),
                 "Sandboxee returned size \\(12\\) exceeding capacity \\(4\\)");
  }
}

TEST(MultiLevelPointersTest,
     SizedByBindingReferencesStructFieldsInNestedStructAndSizedArray) {
  const size_t dims_a[] = {2, 2};  // elem_count = 4
  const size_t dims_b[] = {3, 1};  // elem_count = 3
  ASSERT_EQ(register_hyperslab_var(/*ncid=*/21, /*varid=*/1, dims_a, 2), 22);
  ASSERT_EQ(register_hyperslab_var(/*ncid=*/22, /*varid=*/2, dims_b, 2), 24);

  // Nested struct: `rows = 2`, `elem_count = 4` -> 8 elements.
  // Pass bogus `ncid = -999` and `rows = 0` arguments so unprefixed codegen
  // would look up the wrong composite key or copy 0 elements.
  std::vector<int> nested_out(8, -1);
  NestedHyperslabRequest req = {
      /*tag_id=*/5,
      {/*ncid=*/21, /*varid=*/1, /*rows=*/2, nested_out.data()},
  };
  ASSERT_EQ(fill_nested_hyperslab_request(&req, /*ncid=*/-999, /*rows=*/0), 8);
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(nested_out[i], 500 + 21 * 10 + 1 + i);
  }

  // Sized struct array: two slices referencing distinct `(ncid, varid)` keys.
  std::vector<int> batch_out0(8, -1);  // 2 * 4 = 8
  std::vector<int> batch_out1(9, -1);  // 3 * 3 = 9
  BoundHyperslabSlice batch[2] = {
      {/*ncid=*/21, /*varid=*/1, /*rows=*/2, batch_out0.data()},
      {/*ncid=*/22, /*varid=*/2, /*rows=*/3, batch_out1.data()},
  };
  ASSERT_EQ(fill_hyperslab_slice_batch(batch, 2, /*ncid=*/-999, /*rows=*/0),
            17);
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(batch_out0[i], 21 * 10 + 1 + i);
  }
  for (int i = 0; i < 9; ++i) {
    EXPECT_EQ(batch_out1[i], 22 * 10 + 2 + i);
  }
}

}  // namespace
