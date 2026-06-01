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

#ifndef SANDBOXED_API_ANNOTATIONS_H_
#define SANDBOXED_API_ANNOTATIONS_H_

#include "sandboxed_api/annotations_internal.h"

// NOTE: this functionality is experimental and may change in the future.

// Lists function that are to be sandboxed, or not sandboxed, correspondingly.
//
// For example:
//   SANDBOX_FUNCS(foo_init, foo_destroy, foo_something);
// or:
//   SANDBOX_IGNORE_FUNCS(foo_unused_func, foo_unsupported_signature);
//
// Functions that are not selected by these macros will not be available
// in the sandbox. If none of these macros are used, all functions in the
// library header files are selected for sandboxing. But if used, only one
// of the macros can be used in a given file once.
#define SANDBOX_FUNCS(...) SANDBOX_FUNCS_IMPL(__VA_ARGS__)
#define SANDBOX_IGNORE_FUNCS(...) SANDBOX_IGNORE_FUNCS_IMPL(__VA_ARGS__)

// Pointer argument annotations that denote direction of the pointee data:
// data is input for the sandbox function, output of the sandbox function,
// or both input and output.
//
// For example:
//   void get_dimensions(int* x SANDBOX_OUT_PTR,
//                       int* y SANDBOX_OUT_PTR,
//                       int* z SANDBOX_OUT_PTR);
#define SANDBOX_IN_PTR [[clang::annotate("sandbox", "in_ptr")]]
#define SANDBOX_OUT_PTR [[clang::annotate("sandbox", "out_ptr")]]
#define SANDBOX_INOUT_PTR [[clang::annotate("sandbox", "inout_ptr")]]

// Pointer argument where the data lives in the sandbox, the host can just treat
// it as a handle with which the sandbox can do what it will, and knows what it
// refers to. This pointer will then be invalid in the host.
#define SANDBOX_OPAQUE_PTR [[clang::annotate("sandbox", "sandbox_opaque_ptr")]]

// Pointer argument where the data lives in the host, the sandbox can just treat
// it as a handle with which the host can do what it will, and knows what it
// refers to.
// This pointer will then be invalid in the sandbox.
#define SANDBOX_HOST_OPAQUE_PTR \
  [[clang::annotate("sandbox", "host_opaque_ptr")]]

// Pointer argument annotation that denotes that the pointee data is an array
// with size `elem_size_arg` elements of the pointee type.
// The expression for `elem_size_arg` can be:
// - a constant
// - a reference to another parameter by name
// - simple, side-effect-free arithmetic expressions.
//   NOTE: at the moment, the user is responsible for making sure there is
//   no overflow in such calculations.
// This is similar to "__counted_by" in Clang's proposed `-fbounds-safety`
// annotations.
//
// For example:
//   void my_memcpy(char* dst SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(n),
//                  const char* src SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(n),
//                  size_t n);
#define SANDBOX_ELEM_SIZED_BY(elem_size_arg) \
  [[clang::annotate("sandbox", "elem_sized_by", #elem_size_arg)]]

// Pointer argument annotation that denotes that the pointee data is an array
// with the size `byte_size_arg` bytes (not number of elements).
// Similar to "__sized_by" in Clang's proposed `-fbounds-safety` annotations.
// The allowed expressions for `byte_size_arg` are the same as for
// SANDBOX_ELEM_SIZED_BY.
//
// For example:
//   void my_memset(void* dst SANDBOX_OUT_PTR SANDBOX_BYTE_SIZED_BY(n),
//                  int c, size_t n);
#define SANDBOX_BYTE_SIZED_BY(byte_size_arg) \
  [[clang::annotate("sandbox", "byte_sized_by", #byte_size_arg)]]

// Pointer argument annotation that denotes that the pointee data is an array
// with size determined by an outparam (SANDBOX_OUT_PTR or SANDBOX_INOUT_PTR).
// The array:
// - is caller/host-owned, like `char*`. It is not an outparam like `char**`.
// - `capacity_expr` describes the capacity, in bytes, of the host-owned array.
// - `size_outparam` describes the number of elements. It should be a simple
//   expression including the "*", e.g., `*len` where `len` is the name of the
//   outparam. We currently only support a single outparam, not multiple.
//
// Importantly, the sandbox controls the size!
// To validate, we ask the caller to provide the capacity.
//
// For example:
//   int compress(
//       const char *src SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(src_len),
//       size_t src_len,
//       char* dst SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY_OUTPARAM(*len, cap),
//       size_t* len SANDBOX_OUT_PTR,
//       size_t cap);
#define SANDBOX_ELEM_SIZED_BY_OUTPARAM(size_outparam, capacity_expr)     \
  [[clang::annotate("sandbox", "elem_sized_by_outparam", #size_outparam, \
                    #capacity_expr)]]

// A pointer argument annotation like SANDBOX_ELEM_SIZED_BY_OUTPARAM, but for
// byte sizes (similar to SANDBOX_BYTE_SIZED_BY).
#define SANDBOX_BYTE_SIZED_BY_OUTPARAM(size_outparam, capacity_expr)     \
  [[clang::annotate("sandbox", "byte_sized_by_outparam", #size_outparam, \
                    #capacity_expr)]]

// Pointer argument annotation that denotes that the pointee data is a
// null-terminated C string.
// - `const char*` parameters are supported
// - `const char*` return values are also supported, but need a lifetime
//   annotation (like SANDBOX_LIFETIME_GLOBAL) to indicate how to manage a host
//   copy (if a copy is needed).
//
// Input example:
//   int my_open(const char* path SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED);
// Output examples:
// as a return value:
//   SANDBOX_OUT_PTR SANDBOX_NULL_TERMINATED SANDBOX_LIFETIME_GLOBAL
//   const char* status_to_error(int error_code);
// or, as an outparam:
//   void status_to_error(int error_code,
//                        const char** error_msg SANDBOX_OUT_PTR
//                                               SANDBOX_NULL_TERMINATED
//                                               SANDBOX_LIFETIME_GLOBAL);
#define SANDBOX_NULL_TERMINATED \
  [[clang::annotate("sandbox", "null_terminated")]]

// Annotations for sandboxee and host thunks.
// These just mean that we also emit these into the sandbox / host code.
// They can be used to hook function calls.
#define SANDBOX_SANDBOXEE_THUNK(func_name) \
  [[clang::annotate("sandbox", "sandboxee_thunk", #func_name)]]
#define SANDBOX_HOST_THUNK(func_name) \
  [[clang::annotate("sandbox", "host_thunk", #func_name)]]

// These are for verbatim code snippets that are included in the host code.
#define SANDBOX_HOST_CODE(...)                \
  [[clang::annotate("sandbox", "host_code")]] \
  static constexpr char host_code[] = __VA_ARGS__

// These are for verbatim code snippets that are included in the sandboxee code.
#define SANDBOX_SANDBOXEE_CODE(...)                \
  [[clang::annotate("sandbox", "sandboxee_code")]] \
  static constexpr char sandboxee_code[] = __VA_ARGS__

// This is for host variables that can hold some state between calls.
#define SANDBOX_HOST_STATE_VAR [[clang::annotate("sandbox", "host_state_var")]]

// Indicate that an output pointer points to global memory in the sandbox.
// If we transparently copy the data into the host, it should also have a global
// lifetime.
//
// Support is still in progress, and we currently only support NULL_TERMINATED
// return values and outparam pointers.
//
// See the example under SANDBOX_NULL_TERMINATED.
#define SANDBOX_LIFETIME_GLOBAL \
  [[clang::annotate("sandbox", "lifetime_sandbox_global")]]

#endif  // SANDBOXED_API_ANNOTATIONS_H_
