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

#endif  // SANDBOXED_API_SANDBOX2_TESTCASES_REPLACED_LIBRARY_COMPLEX_SIZE_H_
