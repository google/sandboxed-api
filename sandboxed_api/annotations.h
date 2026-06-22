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

/////////////////////////////////////////////////////////////////////////////
// Lifetime annotations for returned pointers or outparams.

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

// Indicate that a returned pointer (unless null) is an alias of an incoming
// (host-owned) pointer parameter, and thus has the same lifetime as that other
// pointer. If the return value is null, then we return null instead of the
// alias (which may or may not be null).
//
// Given this lifetime information we do not need to allocate and manage
// memory for the output. Thus, we do not need to know the size for array
// outputs. This annotation implies SANDBOX_OUT_PTR (no need to specify both).
//
// This is similar to [[clang::lifetimebound]], but we are focused more
// on avoiding memory management than use-after-free.
//
// This often shows up when functions return an outparam for convenience,
// and the exception for null return values is also common for error handling.
//
// For example:
//
//   SANDBOX_ALIAS_PTR(p)
//   MyStruct* InitStructPartA(MyStruct* p SANDBOX_OUT_PTR)
#define SANDBOX_ALIAS_PTR(param_name) \
  [[clang::annotate("sandbox", "alias_ptr", #param_name)]]

/////////////////////////////////////////////////////////////////////////////
// Binding data to objects, getting from the bindings, and clearing.
// - which can help with host or sandbox copies that need extended lifetimes
//   from a "getter" -> "destroy".
// - or, can help store primitive data from the host given at an earlier
//   point to be used at a later point. E.g. a size during "create" to be
//   used during "get" or "sync" to synchronize a buffer.

// Bind primitive data to the given "context" (could be an opaque pointer, or
// some other pointer we want to bind data to).
// The value is computed *after* the function is called to create the binding.
// NOTE: this is an annotation on the function creating the binding,
// vs other annotations that are attached to one of the pointers involved
// (e.g., the return value pointer, or the context pointer
// being destroyed).
//
// - context: the pointer to the object to bind the data to.
//   - can be the name of a pointer parameter
//   - or SANDBOX_RETURN to bind to the return value
// - type: the type of the data, e.g. size_t.
// - name: the name of the binding, e.g. "size".
// - host_computable_expr: an expression to compute the value in the host.
//
// The bound value can be retrieved, e.g., with SANDBOX_BIND_SIZED_BY_GET_DATA
// for use in SANDBOX_COPY_FROM_AND_BIND_OUT_PTR.
//
// If the context is null, then we do not bind.
//
// For example:
//
//   // Creates an image with the given dimensions.
//   SANDBOX_OPAQUE_PTR
//   SANDBOX_BIND_DATA(SANDBOX_RETURN, "size_t", "size", width * height)
//   Image* create_image(int width, int height);
//
//   // Gets the dimensions of a decompressed image.
//   SANDBOX_BIND_DATA(
//        context, "size_t", "size", dimensions->width * dimensions->height)
//   void get_dimensions(Image* context SANDBOX_OPAQUE_PTR,
//                       Dimensions* dimensions SANDBOX_OUT_PTR);
#define SANDBOX_BIND_DATA(context, type, name, host_computable_expr) \
  [[clang::annotate("sandbox", "bind_data", #context, #type, #name,  \
                    #host_computable_expr)]]

// Shortcut for binding data of type `size_t`.
#define SANDBOX_BIND_SIZE(context, name, host_computable_expr) \
  SANDBOX_BIND_DATA(context, size_t, name, host_computable_expr)

#define SANDBOX_RETURN "$return"

// Output pointer annotation that denotes that the pointee data is an array
// which will be accessed by the host. Thus, we should make a host copy of data
// and not free it until a later point. To extend the lifetime, we bind the
// host copy to the given "context" (could be an opaque pointer, or some other
// pointer we want to bind data to), and only free it when the binding is
// cleared with SANDBOX_CLEAR_BINDINGS.
//
// If the output pointer is null, then we do not copy and bind, and leave it as
// null in the host. We also skip if the context is null (assuming the library
// would have returned null in that case too).
//
// - context: the pointer to the object to bind the host copy to.
// - name: the name of the binding, e.g. "buffer".
// - size_by: an expression that indicates the size of the array in bytes.
//   - can be a constant or simple host-computable expression, e.g.
//     (SANDBOX_BIND_SIZED_BY_EXPR(sizeof(ctx->inline_buffer)))
//   - or, is a null-terminated C-string (SANDBOX_BIND_SIZED_BY_NULL_TERMINATED,
//     note, this is different from SANDBOX_NULL_TERMINATED)
//   - or, a value that should be looked up from a binding
//     (SANDBOX_BIND_SIZED_BY_GET_DATA(context, name))
//
// For example (continuing from the `create_image` of SANDBOX_BIND_DATA):
//
//   SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(
//       context,
//       SANDBOX_BIND_SIZED_BY_GET_DATA(context, "size"))
//   char* get_image_buffer(Image* context SANDBOX_OPAQUE_PTR);
#define SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(context, size_by)           \
  [[clang::annotate("sandbox", "copy_from_and_bind_out_ptr", #context, \
                    SANDBOX_BINDING_GEN_NAME(), size_by)]]

// Annotations for the `size_by` argument of SANDBOX_COPY_FROM_AND_BIND_OUT_PTR.
#define SANDBOX_BIND_SIZED_BY_EXPR(expr) "expr, " #expr
#define SANDBOX_BIND_SIZED_BY_NULL_TERMINATED "null_terminated"
#define SANDBOX_BIND_SIZED_BY_GET_DATA(context, name) \
  "get_data, " #context ", " #name

// An annotation for a pointer parameter (e.g., a context object), that denotes
// that this function will destroy/free the pointee. We use this as a hook to
// also clear all bindings attached to the given pointee, and free any host
// copies, if necessary.
// This assumes that the order of freeing doesn't matter.
// If the context is null, then there should be no bindings to clear.
//
// For example:
//
//   void destroy_image(Image* context SANDBOX_OPAQUE_PTR
//                                     SANDBOX_CLEAR_BINDINGS);
#define SANDBOX_CLEAR_BINDINGS [[clang::annotate("sandbox", "clear_bindings")]]

#endif  // SANDBOXED_API_ANNOTATIONS_H_
