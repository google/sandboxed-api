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

void callback_returning_buffer(const uint8_t* input SANDBOX_IN_PTR
                                   SANDBOX_ELEM_SIZED_BY(in_size),
                               size_t in_size,
                               SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(size)
                                   uint8_t* (*cb)(size_t size, int));

SANDBOX_ALIAS_CALLBACK_RETURN(cb)
uint8_t* ret_alias_func_pointer(const uint8_t* input SANDBOX_IN_PTR
                                    SANDBOX_ELEM_SIZED_BY(in_size),
                                size_t in_size,
                                SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(size)
                                    uint8_t* (*cb)(size_t size, int));

SANDBOX_ALIAS_CALLBACK_RETURN(cb)
uint8_t* ret_alias_std_function(const uint8_t* input SANDBOX_IN_PTR
                                    SANDBOX_ELEM_SIZED_BY(in_size),
                                size_t in_size,
                                SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(size)
                                    std::function<uint8_t*(size_t size, int)>
                                        cb);
SANDBOX_ALIAS_CALLBACK_RETURN(cb)
uint8_t* ret_alias_any_invocable(
    const uint8_t* input SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(in_size),
    size_t in_size,
    SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(size)
        absl::AnyInvocable<uint8_t*(size_t size, int)>
            cb);

SANDBOX_ALIAS_CALLBACK_RETURN(next_out_chunk)
uint8_t* ret_alias_called_multiple_times(
    int num_chunks, size_t chunk_size, bool return_second_last,
    SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(chunk_size)
        SANDBOX_UNINITIALIZED uint8_t* (*next_out_chunk)(size_t chunk_size));

SANDBOX_ALIAS_CALLBACK_RETURN(get_chunk2)
uint8_t* multiple_callbacks_one_ret_alias(
    size_t chunk_size,
    SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(size)
        SANDBOX_UNINITIALIZED uint8_t* (*get_chunk1)(size_t size),
    SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(size)
        SANDBOX_UNINITIALIZED uint8_t* (*get_chunk2)(size_t size));

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

// Callbacks with output pointers
int64_t with_output_prim_pointer(void (*cb)(int64_t* out SANDBOX_OUT_PTR));

int with_output_elem_sized(
    void (*cb)(int* out SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(num_elems),
               size_t),
    size_t num_elems);

int with_output_byte_sized(
    void (*cb)(void* out SANDBOX_OUT_PTR SANDBOX_BYTE_SIZED_BY(num_bytes),
               size_t),
    size_t num_bytes);

// Callbacks with in-out pointers
int64_t with_inout_prim_pointer(void (*cb)(int64_t* inout SANDBOX_INOUT_PTR),
                                int64_t x);

int with_inout_elem_sized(
    void (*cb)(int* inout SANDBOX_INOUT_PTR SANDBOX_ELEM_SIZED_BY(num_elems),
               size_t),
    size_t num_elems);

int with_inout_elem_sized_and_ret_value(
    size_t (*cb)(int* inout SANDBOX_INOUT_PTR SANDBOX_ELEM_SIZED_BY(num_elems),
                 size_t),
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
