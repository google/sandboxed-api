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
        uint8_t* (*next_out_chunk)(size_t chunk_size));

SANDBOX_ALIAS_CALLBACK_RETURN(get_chunk2)
uint8_t* multiple_callbacks_one_ret_alias(
    size_t chunk_size,
    SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(size)
        uint8_t* (*get_chunk1)(size_t size),
    SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(size)
        uint8_t* (*get_chunk2)(size_t size));

}  // extern "C"
