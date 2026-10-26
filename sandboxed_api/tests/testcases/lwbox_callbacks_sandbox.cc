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

#include <cstddef>
#include <cstdint>
#include <functional>

#include "absl/functional/any_invocable.h"
#include "sandboxed_api/annotations.h"

extern "C" {

// Callback typedefs for caller-supplied output buffer allocators. Each
// returned buffer is borrowed for the duration of the outer call and sized by
// the `size` argument passed to the callback.

// Returns a `size`-byte buffer prefilled with `init_val`. Because the library
// only overwrites part of the buffer, the initial contents must be copied into
// the sandbox. Declared as a function type so both function pointers
// (`prefilled_chunk_allocator*`) and C++ functors
// (`std::function<prefilled_chunk_allocator>`,
// `absl::AnyInvocable<prefilled_chunk_allocator>`) can share it.
SANDBOX_LIFETIME_OUTER_CALL SANDBOX_ELEM_SIZED_BY(
    size) typedef uint8_t* prefilled_chunk_allocator(size_t size, int init_val);

// Returns an uninitialized `size`-byte buffer that the library overwrites
// completely, avoiding an initial copy into the sandbox.
SANDBOX_LIFETIME_OUTER_CALL SANDBOX_ELEM_SIZED_BY(size) SANDBOX_UNINITIALIZED
    typedef uint8_t* (*out_chunk_allocator)(size_t size);

void callback_returning_buffer(const uint8_t* input SANDBOX_IN_PTR
                                   SANDBOX_ELEM_SIZED_BY(in_size),
                               size_t in_size, prefilled_chunk_allocator* cb);

SANDBOX_ALIAS_CALLBACK_RETURN(cb)
uint8_t* ret_alias_func_pointer(const uint8_t* input SANDBOX_IN_PTR
                                    SANDBOX_ELEM_SIZED_BY(in_size),
                                size_t in_size, prefilled_chunk_allocator* cb);

// Functor callback parameters (`std::function`, `absl::AnyInvocable`) can
// reference the same function `typedef` as function pointers.
SANDBOX_ALIAS_CALLBACK_RETURN(cb)
uint8_t* ret_alias_std_function(
    const uint8_t* input SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(in_size),
    size_t in_size, std::function<prefilled_chunk_allocator> cb);

SANDBOX_ALIAS_CALLBACK_RETURN(cb)
uint8_t* ret_alias_any_invocable(
    const uint8_t* input SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(in_size),
    size_t in_size, absl::AnyInvocable<prefilled_chunk_allocator> cb);

SANDBOX_ALIAS_CALLBACK_RETURN(next_out_chunk)
uint8_t* ret_alias_called_multiple_times(int num_chunks, size_t chunk_size,
                                         bool return_second_last,
                                         out_chunk_allocator next_out_chunk);

SANDBOX_ALIAS_CALLBACK_RETURN(get_chunk2)
uint8_t* multiple_callbacks_one_ret_alias(size_t chunk_size,
                                          out_chunk_allocator get_chunk1,
                                          out_chunk_allocator get_chunk2);

// Callbacks with input pointers.
int64_t with_input_prim_pointer(
    int64_t (*cb)(const int64_t* input SANDBOX_IN_PTR), int64_t input);

int with_input_elem_sized(
    int (*cb)(const int* input SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(in_size),
              size_t in_size),
    size_t in_size);

int with_input_byte_sized(
    int (*cb)(const void* input SANDBOX_IN_PTR SANDBOX_BYTE_SIZED_BY(num_bytes),
              size_t num_bytes),
    size_t in_size);

int with_input_null_term(
    int (*cb)(const char* input SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED),
    const char* input SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED);

// Callback declared through a typedef. Clang preserves parameter declarations
// and annotations in the typedef's TypeSourceInfo, so the generator processes
// them in the same way as inline callback parameters.
typedef int (*sum_ints_cb)(const int* input SANDBOX_IN_PTR
                               SANDBOX_ELEM_SIZED_BY(in_size),
                           size_t in_size);
int with_typedefd_input_elem_sized(sum_ints_cb cb, size_t in_size);

// Callbacks with output pointers
int64_t with_output_prim_pointer(void (*cb)(int64_t* out SANDBOX_OUT_PTR));

int with_output_elem_sized(
    void (*cb)(int* out SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(out_num_elems),
               size_t out_num_elems),
    size_t num_elems);

int with_output_byte_sized(
    void (*cb)(void* out SANDBOX_OUT_PTR SANDBOX_BYTE_SIZED_BY(out_num_bytes),
               size_t out_num_bytes),
    size_t num_bytes);

// Callbacks with in-out pointers
int64_t with_inout_prim_pointer(void (*cb)(int64_t* inout SANDBOX_INOUT_PTR),
                                int64_t x);

int with_inout_elem_sized(void (*cb)(int* inout SANDBOX_INOUT_PTR
                                         SANDBOX_ELEM_SIZED_BY(inout_num_elems),
                                     size_t inout_num_elems),
                          size_t num_elems);

int with_inout_elem_sized_and_ret_value(
    size_t (*cb)(int* inout SANDBOX_INOUT_PTR
                     SANDBOX_ELEM_SIZED_BY(inout_num_elems),
                 size_t inout_num_elems),
    size_t num_elems);

// With host opaque pointers.
int with_host_opaque(int (*combiner)(void* cb_closure SANDBOX_HOST_OPAQUE_PTR
                                         SANDBOX_ALIAS_PTR(outer_param),
                                     int val,
                                     void* cb_closure2 SANDBOX_HOST_OPAQUE_PTR
                                         SANDBOX_ALIAS_PTR(outer_param2),
                                     // Have a third opaque pointer that is an
                                     // alias of the first.
                                     void* cb_closure3 SANDBOX_HOST_OPAQUE_PTR
                                         SANDBOX_ALIAS_PTR(outer_param)),
                     int val, void* outer_param SANDBOX_HOST_OPAQUE_PTR,
                     void* outer_param2 SANDBOX_HOST_OPAQUE_PTR);

}  // extern "C"
