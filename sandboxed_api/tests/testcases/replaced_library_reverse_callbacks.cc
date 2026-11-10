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

#include "sandboxed_api/tests/testcases/replaced_library_reverse_callbacks.h"

#include <stdint.h>

namespace {

int AddInts(int a, int b) { return a + b; }

int MultiplyInts(int a, int b) { return a * b; }

}  // namespace

extern "C" {

uintptr_t get_adder(int which) {
  switch (which) {
    case 0:
      return reinterpret_cast<uintptr_t>(&AddInts);
    case 1:
      return reinterpret_cast<uintptr_t>(&MultiplyInts);
    default:
      return 0;
  }
}

int apply_twice(int (*cb)(int, int), int a, int b) {
  if (cb == nullptr) {
    return 0;
  }
  return cb(cb(a, b), b);
}

}  // extern "C"
