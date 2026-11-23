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

#include <stddef.h>
#include <stdint.h>

#include "sandboxed_api/annotations.h"
#include "sandboxed_api/tests/testcases/replaced_library_multi_level_ptrs.h"

extern "C" {

// ---------------------------------------------------------------------------
// 1. 2-level opaque handle pointers (`DbHandle**`)
// ---------------------------------------------------------------------------
// A pointer `typedef` describes one level of dereference (`*out_db` /
// `*inout_db` of type `DbHandle*`), while the outer parameter
// (`db_handle_pointee*`, which is `DbHandle**`) carries its own direction
// (`SANDBOX_OUT_PTR` or `SANDBOX_INOUT_PTR`).
typedef struct DbHandle* db_handle_pointee SANDBOX_OPAQUE_PTR;

int open_db_handle(int initial_value,
                   db_handle_pointee* out_db SANDBOX_OUT_PTR);

int read_db_handle(struct DbHandle* db SANDBOX_OPAQUE_PTR);

int update_or_close_db_handle(db_handle_pointee* inout_db SANDBOX_INOUT_PTR,
                              int new_value);

void close_db_handle(
    struct DbHandle* db SANDBOX_OPAQUE_PTR SANDBOX_CLEAR_BINDINGS);

typedef const int* db_bound_pair_pointee SANDBOX_OUT_PTR
    SANDBOX_ELEM_SIZED_BY(2) SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(db);

int get_db_header_and_footer(struct DbHandle* db SANDBOX_OPAQUE_PTR,
                             db_bound_pair_pointee* out_header SANDBOX_OUT_PTR,
                             db_bound_pair_pointee* out_footer SANDBOX_OUT_PTR);

int get_db_alt_slice(struct DbHandle* db SANDBOX_OPAQUE_PTR,
                     db_bound_pair_pointee* out_alt SANDBOX_OUT_PTR);

// ---------------------------------------------------------------------------
// 2. 2-level C-string pointers (`const char* const*` and `const char**`)
// ---------------------------------------------------------------------------
typedef const char* c_str_in_pointee SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED;

typedef const char* static_c_str_out_pointee SANDBOX_OUT_PTR
    SANDBOX_NULL_TERMINATED SANDBOX_LIFETIME_GLOBAL;

typedef const char* static_c_str_inout_pointee SANDBOX_INOUT_PTR
    SANDBOX_NULL_TERMINATED SANDBOX_LIFETIME_GLOBAL;

size_t sum_string_lengths(size_t count,
                          const c_str_in_pointee* strings SANDBOX_IN_PTR
                              SANDBOX_ELEM_SIZED_BY(count),
                          static_c_str_out_pointee* out_status SANDBOX_OUT_PTR);

int match_or_replace_status(
    const static_c_str_inout_pointee* expected_status SANDBOX_IN_PTR,
    static_c_str_inout_pointee* inout_status SANDBOX_INOUT_PTR);

// ---------------------------------------------------------------------------
// 3. 2-level 2D buffer (`int**`)
// ---------------------------------------------------------------------------
// Size expressions on a pointer `typedef` (such as `cols` below) are resolved
// against the sibling parameters of the function where the `typedef` is used.
typedef int* int_row_inout_pointee SANDBOX_INOUT_PTR
    SANDBOX_ELEM_SIZED_BY(cols);

int scale_2d_grid(int_row_inout_pointee* grid SANDBOX_INOUT_PTR
                      SANDBOX_ELEM_SIZED_BY(rows),
                  size_t rows, size_t cols, int factor);

// ---------------------------------------------------------------------------
// 4. 3-level pointers (`int***` and `DbHandle***`)
// ---------------------------------------------------------------------------
// For a 3-level pointer (`T***`), chain two pointer `typedef`s:
//   - The innermost `typedef` describes `T*` (depth 2, `**param`).
//   - The middle `typedef` describes `T**` (depth 1, `*param`) and uses the
//     innermost `typedef` as its pointee type.
//   - The function parameter (`T***`, depth 0) uses a pointer to the middle
//     `typedef`.
typedef int_row_inout_pointee* int_matrix_inout_pointee SANDBOX_INOUT_PTR
    SANDBOX_ELEM_SIZED_BY(rows);

// Case A: `int*** matrix_ptr` is a singleton pointer to a `rows x cols` 2D
// array (`(*matrix_ptr)[rows][cols]`).
int add_to_matrix_via_3level_ptr(int_matrix_inout_pointee* matrix_ptr
                                     SANDBOX_INOUT_PTR,
                                 size_t rows, size_t cols, int delta);

// Case B: `int*** tensor` is a 3D jagged array (`tensor[layers][rows][cols]`),
// reusing the same `int_matrix_inout_pointee` -> `int_row_inout_pointee` chain
// with `SANDBOX_ELEM_SIZED_BY(layers)` on the outer parameter.
int increment_3d_tensor(int_matrix_inout_pointee* tensor SANDBOX_INOUT_PTR
                            SANDBOX_ELEM_SIZED_BY(layers),
                        size_t layers, size_t rows, size_t cols, int delta);

// Case C: `DbHandle*** handle_list_ptr` is a singleton pointer to a
// caller-allocated array `(*handle_list_ptr)[0..count)` of `DbHandle*` opaque
// handles filled by the callee.
typedef db_handle_pointee* db_handle_array_out_pointee SANDBOX_OUT_PTR
    SANDBOX_ELEM_SIZED_BY(count);

int open_db_batch_via_3level_ptr(size_t count, int base_value,
                                 db_handle_array_out_pointee* handle_list_ptr
                                     SANDBOX_INOUT_PTR);

// ---------------------------------------------------------------------------
// 5. `nullptr`-terminated pointer arrays (`const char* const*`, `const
// xmlChar**`)
// ---------------------------------------------------------------------------
// Modeled on Lua's `luaL_checkoption(..., const char *const lst[])` and
// libxml2/expat's `startElementSAXFunc(..., const xmlChar **atts)`.
int check_option_from_list(
    const char* name SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED,
    const char* def SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED,
    const c_str_in_pointee* lst SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED);

typedef const sax_xml_char_t* sax_xml_str_in_pointee SANDBOX_IN_PTR
    SANDBOX_NULL_TERMINATED;

SANDBOX_CALLBACK_IDEMPOTENT typedef void (*sax_start_element_cb)(
    void* ctx SANDBOX_HOST_OPAQUE_PTR,
    const sax_xml_char_t* name SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED,
    sax_xml_str_in_pointee* atts SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED);

void dispatch_sax_start_element(
    void* ctx SANDBOX_HOST_OPAQUE_PTR,
    const sax_xml_char_t* name SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED,
    sax_xml_str_in_pointee* atts SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED,
    sax_start_element_cb cb);

sax_start_element_cb get_default_sax_start_element_cb();
int get_default_sax_attr_byte_sum();
void reset_default_sax_attr_byte_sum();

size_t measure_non_const_c_strings(
    char* str SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED,
    unsigned char* ustr SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED);

void uppercase_and_clobber_terminator(char* str SANDBOX_INOUT_PTR
                                          SANDBOX_NULL_TERMINATED,
                                      int clobber_terminator);

SANDBOX_ANNOTATE_STRUCT(struct TaggedIntSlice {
  const char* label SANDBOX_NULL_TERMINATED;
  int* elems SANDBOX_ELEM_SIZED_BY(count);
  size_t count;
};)

int sum_and_scale_tagged_slice(struct TaggedIntSlice* slice SANDBOX_INOUT_PTR,
                               int factor);

int fill_up_to_inout_count(
    int* dst SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(*inout_count),
    size_t* inout_count SANDBOX_INOUT_PTR, size_t actual_count, int base_value);

int retain_inout_db_buffer(
    struct DbHandle* db SANDBOX_OPAQUE_PTR,
    int* buf SANDBOX_INOUT_PTR SANDBOX_ELEM_SIZED_BY(*inout_count)
        SANDBOX_RETAIN_AND_BIND(db),
    size_t* inout_count SANDBOX_INOUT_PTR, size_t actual_count, int base_value);

int fill_or_fail_outparam_sized(
    int* dst SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY_OUTPARAM(*out_count,
                                                            capacity_bytes),
    size_t capacity_bytes, size_t* out_count SANDBOX_OUT_PTR, int should_fail,
    size_t write_count, int fill_value);

int retain_signed_inout_db_buffer(
    struct DbHandle* db SANDBOX_OPAQUE_PTR,
    int* buf SANDBOX_INOUT_PTR SANDBOX_ELEM_SIZED_BY(*inout_count)
        SANDBOX_RETAIN_AND_BIND(db),
    int* inout_count SANDBOX_INOUT_PTR, int actual_count, int base_value);

int sum_in_ptr_with_signed_inout_count(
    const int* src SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(*inout_count),
    int* inout_count SANDBOX_INOUT_PTR, int updated_count);

typedef int (*out_counted_fill_cb)(
    int* dst SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY_OUTPARAM(*out_count,
                                                            cap_bytes),
    size_t cap_bytes, int* out_count SANDBOX_OUT_PTR, int should_fail);

int invoke_out_counted_callback(out_counted_fill_cb cb,
                                int initial_out_count_garbage, int should_fail,
                                int* observed_first_elem SANDBOX_OUT_PTR);

typedef int (*inout_counted_xform_cb)(
    int* buf SANDBOX_INOUT_PTR SANDBOX_ELEM_SIZED_BY_OUTPARAM(*inout_count,
                                                              cap_bytes),
    size_t cap_bytes, int* inout_count SANDBOX_INOUT_PTR, int should_fail);

int invoke_inout_counted_callback(inout_counted_xform_cb cb,
                                  int initial_inout_count, int should_fail,
                                  int* final_inout_count SANDBOX_OUT_PTR);

int is_marshaled_copy(const int* probe SANDBOX_IN_PTR, uintptr_t caller_addr);

}  // extern "C"
