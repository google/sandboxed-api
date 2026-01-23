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

#include "absl/algorithm/container.h"
#include "absl/types/span.h"

namespace internal {
int accumulate(absl::Span<const int* const> values) {
  return absl::c_accumulate(values, 0, [](int acc, const int* val) {
    return val ? acc + *val : acc;
  });
}
}  // namespace internal

extern "C" int accumulate(int* a, int* b, int* c, int* d, int* e, int* f,
                          int* g, int* h) {
  return internal::accumulate({a, b, c, d, e, f, g, h});
}
