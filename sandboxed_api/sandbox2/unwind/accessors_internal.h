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
//
// Do not include this header directly. Use accessors.h instead.
// The `accessors_internal.*` files are implementation details of the
// `accessors_.*` files. Those are only needed because GCC does not support
// including <stdatomic.h> from C++ code.

#ifndef SANDBOXED_API_SANDBOX2_UNWIND_ACCESSORS_INTERNAL_H_
#define SANDBOXED_API_SANDBOX2_UNWIND_ACCESSORS_INTERNAL_H_

#include "libunwind.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

int Sandbox2FindUnwindTable(unw_addr_space_t as, void* map, size_t map_size,
                            const char* path, unw_word_t segbase,
                            unw_word_t mapoff, unw_word_t ip,
                            unw_proc_info_t* pi, int need_unwind_info,
                            void* arg);

#if defined(__cplusplus) || defined(c_plusplus)
}  // extern "C"
#endif

#endif  // SANDBOXED_API_SANDBOX2_UNWIND_ACCESSORS_INTERNAL_H_
