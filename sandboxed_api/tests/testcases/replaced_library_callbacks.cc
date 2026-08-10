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

#include "sandboxed_api/tests/testcases/replaced_library_callbacks.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>

#include "absl/functional/any_invocable.h"

extern "C" {

int callback_with_primitives(int (*cb)(int, int), int a, int b) {
  if (cb == nullptr) return 0;
  return cb(a, b);
}

}  // extern "C"

namespace {

template <typename T>
uint8_t* callback_ret_alias_impl(const uint8_t* input, size_t in_size, T cb) {
  if (cb == nullptr || input == nullptr) return nullptr;

  int init_val = 0xCA;
  uint8_t* output = cb(in_size * 2 + 2, init_val);
  if (output == nullptr) return nullptr;

  memcpy(output, input, in_size);
  memcpy(output + in_size, input, in_size);
  // Note: we leave the last two bytes with the `init_val`.
  return output;
}

}  // namespace

extern "C" {

uint8_t* ret_alias_func_pointer(const uint8_t* input, size_t in_size,
                                uint8_t* (*cb)(size_t, int)) {
  return callback_ret_alias_impl(input, in_size, cb);
}

void callback_returning_buffer(const uint8_t* input, size_t in_size,
                               uint8_t* (*cb)(size_t, int)) {
  (void)ret_alias_func_pointer(input, in_size, cb);
}

uint8_t* ret_alias_std_function(const uint8_t* input, size_t in_size,
                                std::function<uint8_t*(size_t, int)> cb) {
  return callback_ret_alias_impl(input, in_size, std::move(cb));
}

uint8_t* ret_alias_any_invocable(const uint8_t* input, size_t in_size,
                                 absl::AnyInvocable<uint8_t*(size_t, int)> cb) {
  return callback_ret_alias_impl(input, in_size, std::move(cb));
}

uint8_t* ret_alias_called_multiple_times(int num_chunks, size_t chunk_size,
                                         bool return_second_last,
                                         uint8_t* (*next_out_chunk)(size_t)) {
  if (next_out_chunk == nullptr) return nullptr;
  uint8_t* second_last_chunk = nullptr;
  uint8_t* last_chunk = nullptr;
  for (int i = 0; i < num_chunks; ++i) {
    second_last_chunk = last_chunk;
    last_chunk = next_out_chunk(chunk_size);
    if (last_chunk == nullptr) {
      return nullptr;
    }
    memset(last_chunk, i, chunk_size);
  }
  return return_second_last ? second_last_chunk : last_chunk;
}

uint8_t* multiple_callbacks_one_ret_alias(size_t chunk_size,
                                          uint8_t* (*get_chunk1)(size_t),
                                          uint8_t* (*get_chunk2)(size_t)) {
  if (get_chunk1 == nullptr || get_chunk2 == nullptr) return nullptr;
  uint8_t* chunk1 = get_chunk1(chunk_size);
  if (chunk1 == nullptr) return nullptr;
  uint8_t* chunk2 = get_chunk2(chunk_size);
  if (chunk2 == nullptr) return nullptr;
  memset(chunk1, 0xCA, chunk_size);
  memset(chunk2, 0xFE, chunk_size);
  return chunk2;
}

}  // extern "C"
