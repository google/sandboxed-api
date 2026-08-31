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

#ifndef SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_CALLBACKS_H_
#define SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_CALLBACKS_H_

#include <cstddef>
#include <cstdint>
#include <functional>

#include "absl/functional/any_invocable.h"

extern "C" {

int callback_with_primitives(int (*cb)(int, int), int a, int b);

// Given a callback, asks for a buffer that is 2*in_size + 2 bytes,
// and asks the callback to initialize it some initial value.
// This then copies the input buffer to the first and second half of that
// output buffer, leaving the last two bytes with the initial value.
void callback_returning_buffer(const uint8_t* input, size_t in_size,
                               uint8_t* (*cb)(size_t, int));

// Like `callback_returning_buffer` but also returns the output buffer
// (alias).
uint8_t* ret_alias_func_pointer(const uint8_t* input, size_t in_size,
                                uint8_t* (*cb)(size_t, int));

uint8_t* ret_alias_std_function(const uint8_t* input, size_t in_size,
                                std::function<uint8_t*(size_t, int)> cb);

uint8_t* ret_alias_any_invocable(const uint8_t* input, size_t in_size,
                                 absl::AnyInvocable<uint8_t*(size_t, int)> cb);

// Fills output chunks given by a callback, up to num_chunks, each of size
// chunk_size. This will fully overwrite the callback's returned buffers
// so the initial values are not important.
// Returns the last output chunk for convenience or the second last one if
// `return_second_last` is true (or NULL on error).
uint8_t* ret_alias_called_multiple_times(int num_chunks, size_t chunk_size,
                                         bool return_second_last,
                                         uint8_t* (*next_out_chunk)(size_t));

// Multiple callbacks returning output buffers. This will fully overwrite the
// callback's return buffer so the initial values are not important. Returns
// output of the second callback (alias).
uint8_t* multiple_callbacks_one_ret_alias(size_t chunk_size,
                                          uint8_t* (*get_chunk1)(size_t),
                                          uint8_t* (*get_chunk2)(size_t));

// Callbacks with input pointers.
int64_t with_input_prim_pointer(int64_t (*cb)(const int64_t*), int64_t input);

int with_input_elem_sized(int (*cb)(const int*, size_t), size_t in_size);

int with_input_byte_sized(int (*cb)(const void*, size_t), size_t in_size);

int with_input_null_term(int (*cb)(const char*), const char* input);

}  // extern "C"

#endif  // SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_CALLBACKS_H_
