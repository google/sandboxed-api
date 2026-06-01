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

// Test library for cc_sandboxed_library rule, for APIs with complex
// array size annotations.

#ifndef SANDBOXED_API_SANDBOX2_TESTCASES_REPLACED_LIBRARY_COMPLEX_SIZE_H_
#define SANDBOXED_API_SANDBOX2_TESTCASES_REPLACED_LIBRARY_COMPLEX_SIZE_H_

#include <cstddef>

// Copy buffers with size dependent on width and height (and two bytes per
// cell). We also add a special "header" byte to the beginning of the buffer.
// The total size is (1 + (width * height * 2)) bytes.
void mylib_copy_image(const char* src, char* dst, size_t width, size_t height);

// Byte size fill (ignore the type of the pointer)
void mylib_fill_bytes(void* dst, char fill_value, size_t bytes);

// Tests an size argument that is an input pointer (instead of a scalar)
void mylib_copy_with_inptr_len(int* dst, const int* src, const size_t* len);

// Tests an output pointer for the size. Copies some data to dst (trusting that
// there is sufficient capacity), and sets number of amount copied to `*len`.
void mylib_set_with_outptr_len(int buf_num, int* dst, size_t* len);

// A copy of `mylib_set_with_outptr_len` that is more friendly to sandboxing.
// It makes explicit the capacity of the dst buffer as another parameter.
void mylib_set_with_outptr_len_capacity(int buf_num, int* dst, size_t capacity,
                                        size_t* len);

// Similar to `mylib_set_with_outptr_len_capacity`, with byte sizes.
void mylib_set_with_outptr_bytes_capacity(int buf_num, void* dst,
                                          size_t capacity, size_t* num_bytes);

// Tests an input/output pointer for the size. Updates the data in `src_dst`,
// based on the incoming data in `src_dst`. The `src_dst` has capacity
// `capacity` in bytes and starts with `*len` bytes already filled. After the
// call, there will be `*len` bytes filled.
void mylib_set_with_inoutptr_len(int* src_dst, size_t capacity, size_t* len);

#endif  // SANDBOXED_API_SANDBOX2_TESTCASES_REPLACED_LIBRARY_COMPLEX_SIZE_H_
