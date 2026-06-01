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

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string_view>

void mylib_copy_image(const char* src, char* dst, size_t width, size_t height) {
  dst[0] = 0xBC;  // special "header" byte
  memcpy(dst + 1, src + 1, (width * height * 2));
}

void mylib_fill_bytes(void* dst, char fill_value, size_t bytes) {
  memset(dst, fill_value, bytes);
}

void mylib_copy_with_inptr_len(int* dst, const int* src, const size_t* len) {
  memcpy(dst, src, *len * sizeof(int));
}

static constexpr int kZero[] = {0, 0, 0, 0, 0};
static constexpr int kOne[] = {1, 1, 1};
static constexpr int kUnknown[] = {9, 9, 9, 9, 9, 9, 9};
static constexpr size_t kBytesNeededForInts =
    std::max({std::size(kZero), std::size(kOne), std::size(kUnknown)}) *
    sizeof(int);

void mylib_set_with_outptr_len(int buf_num, int* dst, size_t* len) {
  auto do_copy = [&](const int* src, size_t nelems) {
    memcpy(dst, src, nelems * sizeof(int));
    *len = nelems;
  };
  switch (buf_num) {
    case 0: {
      do_copy(kZero, std::size(kZero));
      return;
    }
    case 1: {
      do_copy(kOne, std::size(kOne));
      return;
    }
    default: {
      do_copy(kUnknown, std::size(kUnknown));
      return;
    }
  }
}

void mylib_set_with_outptr_len_capacity(int buf_num, int* dst, size_t capacity,
                                        size_t* len) {
  if (capacity < kBytesNeededForInts) {
    *len = 0;
    return;
  }
  mylib_set_with_outptr_len(buf_num, dst, len);
}

void mylib_set_with_outptr_bytes_capacity(int buf_num, void* dst,
                                          size_t capacity, size_t* num_bytes) {
  if (capacity < kBytesNeededForInts) {
    *num_bytes = 0;
    return;
  }
  size_t size;
  mylib_set_with_outptr_len_capacity(buf_num, reinterpret_cast<int*>(dst),
                                     capacity, &size);
  *num_bytes = size * sizeof(int);
}

void mylib_set_with_inoutptr_len(int* src_dst, size_t capacity, size_t* len) {
  if (capacity < kBytesNeededForInts) {
    *len = 0;
    return;
  }
  int buf_num = -1;
  if (*len == sizeof(kZero) / sizeof(int) &&
      memcmp(src_dst, kZero, sizeof(kZero)) == 0) {
    buf_num = 1;
  } else if (*len == sizeof(kOne) / sizeof(int) &&
             memcmp(src_dst, kOne, sizeof(kOne)) == 0) {
    buf_num = 2;
  }
  mylib_set_with_outptr_len(buf_num, src_dst, len);
}
