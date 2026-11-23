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

#ifndef SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_MULTI_LEVEL_PTRS_H_
#define SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_MULTI_LEVEL_PTRS_H_

#include <stddef.h>

extern "C" {

// Opaque handle managed inside the sandboxed library (modeled on `sqlite3*`).
struct DbHandle;

// 2-level pointer (`DbHandle**`) returning a newly allocated opaque handle
// (modeled on `sqlite3_open(const char* filename, sqlite3** ppDb)`).
int open_db_handle(int initial_value, struct DbHandle** out_db);

// Reads the current value from an opaque `DbHandle*`.
int read_db_handle(struct DbHandle* db);

// 2-level pointer (`DbHandle**`) that either updates the existing handle in
// place (when `new_value >= 0`) or closes it and sets `*inout_db = nullptr`
// (when `new_value < 0`).
int update_or_close_db_handle(struct DbHandle** inout_db, int new_value);

// Destroys a `DbHandle*`.
void close_db_handle(struct DbHandle* db);

// Populates `*out_header` and `*out_footer` with pointers to two internal
// 2-element slices owned by `db` (`SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(db)`).
int get_db_header_and_footer(struct DbHandle* db, const int** out_header,
                             const int** out_footer);

// Populates `*out_alt` with a pointer to an internal 2-element alternate slice
// owned by `db`, reusing the same pointee typedef as
// `get_db_header_and_footer`.
int get_db_alt_slice(struct DbHandle* db, const int** out_alt);

// 2-level C-string array input (`const char* const* strings`) combined with a
// 2-level static C-string output (`const char** out_status`).
// Returns the total byte length of all non-null strings in `strings[0..count)`
// and sets `*out_status` to a static status string (`"ok"` or `"empty"`).
size_t sum_string_lengths(size_t count, const char* const* strings,
                          const char** out_status);

// Compares `*expected_status` against `*inout_status` (both 2-level C-string
// pointers whose pointee typedef is `SANDBOX_LIFETIME_GLOBAL`), replacing
// `*inout_status` with `"matched"` (and returning 1) when equal or `"mismatch"`
// (and returning 0) otherwise.
int match_or_replace_status(const char* const* expected_status,
                            const char** inout_status);

// 2-level 2D buffer (`int** grid`): scales each element `grid[r][c]` in place
// by `factor` (skipping any null row `grid[r] == nullptr`) and returns the sum
// of all scaled elements.
int scale_2d_grid(int** grid, size_t rows, size_t cols, int factor);

// 3-level pointer (`int*** matrix_ptr`): `matrix_ptr` is a singleton pointer
// to a 2D array (`(*matrix_ptr)[rows][cols]`). Adds `delta` to every element
// `(*matrix_ptr)[r][c]` and returns the sum of all updated elements.
int add_to_matrix_via_3level_ptr(int*** matrix_ptr, size_t rows, size_t cols,
                                 int delta);

// 3-level pointer (`int*** tensor`): a 3D jagged array of shape
// `layers x rows x cols` (`tensor[l][r][c]`). Adds `delta` to every element in
// every non-null layer/row and returns the sum of all updated elements.
int increment_3d_tensor(int*** tensor, size_t layers, size_t rows, size_t cols,
                        int delta);

// 3-level pointer (`DbHandle*** handle_list_ptr`): `handle_list_ptr` is a
// singleton pointer to a caller-allocated array `(*handle_list_ptr)[0..count)`
// of `DbHandle*` slots, which the callee populates with newly opened handles
// initialized to `base_value + i`.
int open_db_batch_via_3level_ptr(size_t count, int base_value,
                                 struct DbHandle*** handle_list_ptr);

// `nullptr`-terminated array of `const char*` option strings (modeled on Lua's
// `luaL_checkoption(lua_State *L, int arg, const char *def,
// const char *const lst[])`).
// Searches `lst` (terminated by `nullptr`) for `name` (or `def` when
// `name` is null or empty) and returns the matching 0-based index, or -1 if
// not found.
int check_option_from_list(const char* name, const char* def,
                           const char* const lst[]);

// libxml2 `xmlChar` (`unsigned char`) and SAX2 `startElementSAXFunc` callback
// signature (`void (*)(void *ctx, const xmlChar *name, const xmlChar **atts)`),
// where `atts` is a `nullptr`-terminated array of `const xmlChar*` attribute
// key/value pairs (`{"id", "42", "lang", "en", nullptr}`).
typedef unsigned char sax_xml_char_t;
typedef void (*sax_start_element_cb)(void* ctx, const sax_xml_char_t* name,
                                     const sax_xml_char_t** atts);

void dispatch_sax_start_element(void* ctx, const sax_xml_char_t* name,
                                const sax_xml_char_t** atts,
                                sax_start_element_cb cb);

sax_start_element_cb get_default_sax_start_element_cb();
int get_default_sax_attr_byte_sum();
void reset_default_sax_attr_byte_sum();

}  // extern "C"

#endif  // SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_MULTI_LEVEL_PTRS_H_
