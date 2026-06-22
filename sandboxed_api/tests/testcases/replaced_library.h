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

// Test library for sapi_replacement_library rule.
// It's supposed to include all patterns that we support for transparent
// sandboxing. The test for the library runs with normal and sandboxed
// replacement library.

#ifndef SANDBOXED_API_SANDBOX2_TESTCASES_REPLACED_LIBRARY_H_
#define SANDBOXED_API_SANDBOX2_TESTCASES_REPLACED_LIBRARY_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/strings/string_view.h"
#include "sandboxed_api/annotations.h"
#include "sandboxed_api/annotations_unimplemented.h"
#include "sandboxed_api/tests/testcases/replaced_library_enum.h"
#include "sandboxed_api/tests/testcases/replaced_library_struct.h"

bool mylib_is_sandboxed();

void mylib_scalar_types(int a0, float a1, double a2, int64_t a3, char a4,
                        bool a5, size_t a6);
int mylib_add(int x, int y);

MyLibEnum mylib_take_enum(MyLibEnum e);

// Pass in a host pointer but the sandbox can't do much with it.
void mylib_take_host_opaque_ptr(void* ptr SANDBOX_HOST_OPAQUE_PTR);

std::string mylib_copy(const std::string& src);
void mylib_copy(absl::string_view src, std::string& dst);
void mylib_copy_raw(const char* src SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(size),
                    char* dst SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(size),
                    size_t size);

size_t mylib_strlen(const char* str SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED);

SANDBOX_OUT_PTR SANDBOX_NULL_TERMINATED SANDBOX_LIFETIME_GLOBAL const char*
mylib_get_const_c_str(int i);
SANDBOX_OUT_PTR SANDBOX_NULL_TERMINATED SANDBOX_LIFETIME_GLOBAL const char*
mylib_get_other_c_str(int i);

void mylib_get_inoutparam_c_str(
    const char** in_out SANDBOX_INOUT_PTR SANDBOX_NULL_TERMINATED
        SANDBOX_LIFETIME_GLOBAL);
void mylib_get_outparam_c_str(int i, const char** dst SANDBOX_OUT_PTR
                                         SANDBOX_NULL_TERMINATED
                                             SANDBOX_LIFETIME_GLOBAL);
void mylib_get_in_outparam_c_str(
    const char* src SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED,
    const char** dst SANDBOX_OUT_PTR SANDBOX_NULL_TERMINATED
        SANDBOX_LIFETIME_GLOBAL,
    const char** dst2 SANDBOX_OUT_PTR SANDBOX_NULL_TERMINATED
        SANDBOX_LIFETIME_GLOBAL);

// memsets `dst` with `value` up to `size` bytes.
// Returns `dst` for convenience.
SANDBOX_ALIAS_PTR(dst)
char* mylib_fill_outbuffer_returning_alias(char* dst SANDBOX_OUT_PTR
                                               SANDBOX_BYTE_SIZED_BY(size),
                                           int value, size_t size);

// Initializes `s` with `value`. Returns `s` for convenience.
// If `value` is negative, then consider that an error and return nullptr.
// The return value is not always an alias of `s` (it could be null).
SANDBOX_ALIAS_PTR(s)
PrimitiveStruct* mylib_struct_returning_alias(
    PrimitiveStruct* s SANDBOX_OUT_PTR, int value);

double mylib_in_prim_struct_pointer(const PrimitiveStruct* p SANDBOX_IN_PTR);
void mylib_out_prim_struct_pointer(PrimitiveStruct* p SANDBOX_OUT_PTR);
void mylib_inout_prim_struct_pointer(PrimitiveStruct* p SANDBOX_INOUT_PTR);

double mylib_in_prim_struct_array(const PrimitiveStruct* p SANDBOX_IN_PTR
                                      SANDBOX_ELEM_SIZED_BY(num),
                                  size_t num);
void mylib_out_prim_struct_array(
    PrimitiveStruct* p SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(num), size_t num);
void mylib_inout_prim_struct_array(PrimitiveStruct* p SANDBOX_INOUT_PTR
                                       SANDBOX_ELEM_SIZED_BY(num),
                                   size_t num);

void mylib_expected_syscall1();
void mylib_expected_syscall2();
void mylib_unexpected_syscall1();
void mylib_unexpected_syscall2();

// This function is not supported, but we will exclude it using
// SANDBOX_FUNCS/SANDBOX_IGNORE_FUNCS annotations.
void mylib_func_with_unsupported_arg(union mylib_union* arg);

// This function has a SANDBOX_UNSUPPORTED annotation and should be ignored
// without causing errors.
SANDBOX_UNSUPPORTED("This function is not supported for testing purposes.")
void mylib_func_with_todo(
    const char* arg SANDBOX_UNIMPLEMENTED_TEST SANDBOX_IN_PTR);

#endif  // SANDBOXED_API_SANDBOX2_TESTCASES_REPLACED_LIBRARY_H_
