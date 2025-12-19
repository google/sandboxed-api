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

bool mylib_is_sandboxed();

void mylib_scalar_types(int a0, float a1, double a2, int64_t a3, char a4,
                        bool a5, size_t a6);
int mylib_add(int x, int y);
std::string mylib_copy(const std::string& src);
void mylib_copy(absl::string_view src, std::string& dst);
void mylib_copy_raw(const char* src SANDBOX_ELEM_SIZED_BY(size),
                    char* dst SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(size),
                    size_t size);

void mylib_expected_syscall1();
void mylib_expected_syscall2();
void mylib_unexpected_syscall1();
void mylib_unexpected_syscall2();

// This function is not supported, but we will exclude it using
// SANDBOX_FUNCS/SANDBOX_IGNORE_FUNCS annotations.
void mylib_func_with_unsupported_arg(union mylib_union* arg);

#endif  // SANDBOXED_API_SANDBOX2_TESTCASES_REPLACED_LIBRARY_H_
