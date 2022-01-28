// Copyright 2019 Google LLC
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

// A binary doing various malloc calls to check that the malloc policy works as
// expected.

#include <cstdlib>
#include <vector>

void test() {
  std::vector<void*> ptrs;

  for (size_t n = 1; n <= 0x1000000; n *= 2) {
    void* buf = malloc(n);
    if (buf == nullptr) {
      exit(EXIT_FAILURE);
    }
    ptrs.push_back(buf);
  }

  for (size_t n = 1; n <= 0x1000000; n *= 2) {
    void* buf = calloc(5, n);
    if (buf == nullptr) {
      exit(EXIT_FAILURE);
    }
    ptrs.push_back(buf);
  }

  for (int n = 0; n < ptrs.size(); n++) {
    void* buf = realloc(ptrs[n], 100);
    if (buf == nullptr) {
      exit(EXIT_FAILURE);
    }
    ptrs[n] = buf;
  }

  for (auto ptr : ptrs) {
    free(ptr);
  }
  ptrs.clear();

  // Apply a bit of memory pressure, to trigger alternate allocator behaviors.
  for (size_t n = 0; n < 0x200; n++) {
    void* buf = malloc(0x400);
    if (buf == nullptr) {
      exit(EXIT_FAILURE);
    }
    ptrs.push_back(buf);
  }

  for (auto ptr : ptrs) {
    free(ptr);
  }
}

int main() {
  test();
  return EXIT_SUCCESS;
}
