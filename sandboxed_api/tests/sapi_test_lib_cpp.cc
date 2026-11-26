// Copyright 2025 Google LLC
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

#include <cstdlib>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/types/span.h"
#include "sandboxed_api/lenval_core.h"

namespace sapi_test {

int accumulate(absl::Span<const int* const> values) {
  return absl::c_accumulate(values, 0, [](int acc, const int* val) {
    return val ? acc + *val : acc;
  });
}

}  // namespace sapi_test

extern "C" int accumulate(int* a, int* b, int* c, int* d, int* e, int* f,
                          int* g, int* h) {
  return sapi_test::accumulate({a, b, c, d, e, f, g, h});
}

// Takes `FuncCall::kArgsMax` (16) arguments. Each argument is weighted by its
// position, so a dropped or reordered argument changes the result.
extern "C" int weighted_sum16(int a1, int a2, int a3, int a4, int a5, int a6,
                              int a7, int a8, int a9, int a10, int a11, int a12,
                              int a13, int a14, int a15, int a16) {
  return a1 + 2 * a2 + 3 * a3 + 4 * a4 + 5 * a5 + 6 * a6 + 7 * a7 + 8 * a8 +
         9 * a9 + 10 * a10 + 11 * a11 + 12 * a12 + 13 * a13 + 14 * a14 +
         15 * a15 + 16 * a16;
}

extern "C" bool compare_self_symbol(void* self) {
  return self == reinterpret_cast<void*>(&compare_self_symbol);
}

extern "C" ABSL_ATTRIBUTE_NO_SANITIZE_CFI int call_callback(int (*cb)(int, int),
                                                            int a, int b) {
  return cb(a, b);
}

extern "C" ABSL_ATTRIBUTE_NO_SANITIZE_CFI int call_callback_with_ptr(
    int (*cb)(int*), int* ptr) {
  return cb(ptr);
}

extern "C" ABSL_ATTRIBUTE_NO_SANITIZE_CFI int call_callback_loop(int (*cb)(int),
                                                                 int count) {
  int sum = 0;
  for (int i = 0; i < count; ++i) {
    sum += cb(i);
  }
  return sum;
}

extern "C" void clear_lenval(sapi::LenValStruct* input, bool free_data) {
  if (free_data) {
    free(input->data);
    input->data = nullptr;
  }
  input->size = 0;
}
