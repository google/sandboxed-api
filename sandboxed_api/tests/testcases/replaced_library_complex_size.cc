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
#include <cstdlib>
#include <cstring>

void mylib_copy_image(const char* src, char* dst, size_t width, size_t height) {
  dst[0] = 0xBC;  // special "header" byte
  memcpy(dst + 1, src + 1, (width * height * 2));
}

void mylib_fill_bytes(void* dst, char fill_value, size_t bytes) {
  memset(dst, fill_value, bytes);
}
