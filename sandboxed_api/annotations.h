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

// Annotations for transparently sandboxing libraries through automatically
// generating SAPI wrappers.

#ifndef SANDBOXED_API_ANNOTATIONS_H_
#define SANDBOXED_API_ANNOTATIONS_H_

#include "sandboxed_api/annotations_internal.h"

// NOTE: this functionality is experimental and may change in the future.

// Marks a public function declaration as unsupported for sandboxing, with a
// brief explanation of why. By default, all non-inline external functions in
// the library header files are selected for sandboxing; functions marked with
// `SANDBOX_UNSUPPORTED` are excluded and will not have wrappers generated.
//
// For example:
//   SANDBOX_UNSUPPORTED("variadic arguments are not supported")
//   int foo_printf(const char* fmt, ...);
#define SANDBOX_UNSUPPORTED(reason) \
  [[clang::annotate("sandbox", "unsupported", reason)]]

// Pointer argument annotations that denote direction of the pointee data:
// data is input for the sandbox function, output of the sandbox function,
// or both input and output.
//
// For example:
//   void get_dimensions(int* x SANDBOX_OUT_PTR,
//                       int* y SANDBOX_OUT_PTR,
//                       int* z SANDBOX_OUT_PTR);
//
// For callbacks, synchronous callbacks passed as function-pointer parameters
// (`R (*cb)(Args...)`) and invoked only during the outer call are supported
// directly (no special macro is needed on the function pointer itself). We
// currently assume that the callback is a host function and that the caller is
// a sandbox function. In that case:
// - IN still denotes that the data is input to the function (the callback,
//   so syncs from sandbox to host),
// - For outparams, OUT still denotes data flows out of the function (the
//   callback, so syncs from host to sandbox).
//
// On a callback, place pointer annotations on the callback's own pointer
// parameters. If the library header declares the callback via a `typedef`,
// expand the function pointer inline in the function declaration when its
// pointer parameters need per-call annotations, or annotate the `typedef`
// declaration (which is required when using `SANDBOX_LIFETIME_OUTER_CALL` on a
// callback return value).
//
// Callbacks are not supported when:
// - The sandbox retains the callback to invoke after the outer call returns
//   (persistent callbacks, such as error or log handlers).
// - The function pointer is a struct member.
// - The callback takes `long double`, `__int128`, by-value struct, or variadic
//   (`...`) arguments, more than 10 arguments, or returns a function pointer.
//
// Placement and type support rules:
// - Parameter annotations are placed after the parameter name (for example,
//   `int* x SANDBOX_OUT_PTR`). Every annotated parameter must have a name. For
//   a function-pointer parameter `R (*cb)(Args...)`, any attribute on the
//   parameter itself must go inside the first parentheses right after the
//   parameter name (`R (*cb ATTR)(Args...)`). For array parameters (`T a[]` or
//   `T a[N]`), use pointer syntax `T* a` so the attribute attaches to the
//   parameter declaration rather than the array type (with
//   `SANDBOX_ELEM_SIZED_BY(N)` for fixed-size `T a[N]`).
// - Return-value and function-level annotations (such as `SANDBOX_OPAQUE_PTR`,
//   `SANDBOX_OUT_PTR`, `SANDBOX_COPY_FROM_AND_BIND_OUT_PTR`,
//   `SANDBOX_LIFETIME_GLOBAL`, or `SANDBOX_BIND_DATA`) are placed before the
//   return type of the function declaration.
// - Conditionally-written outparams (only written on some paths and left
//   unmodified otherwise) must use `SANDBOX_INOUT_PTR` rather than
//   `SANDBOX_OUT_PTR` so the caller's initial value is preserved.
// - Non-opaque pointer return values support `SANDBOX_LIFETIME_GLOBAL` (for
//   null-terminated `const char*`), `SANDBOX_ALIAS_PTR` /
//   `SANDBOX_ALIAS_CALLBACK_RETURN`, and `SANDBOX_COPY_FROM_AND_BIND_OUT_PTR`
//   (for primitive or deeply trivially copyable struct pointees).
// - Passing structs or unions by value is not supported; pointers to trivially
//   copyable structs are supported via `SANDBOX_IN_PTR`, `SANDBOX_OUT_PTR`, or
//   `SANDBOX_INOUT_PTR`.
// - On a `T**` outparam, `SANDBOX_OUT_PTR` alone only syncs the outer pointer
//   value (`*out`), which is sufficient when `T*` is an opaque handle; to copy
//   pointee data to the host, combine with `SANDBOX_LIFETIME_GLOBAL` (for
//   `const char**`) or `SANDBOX_COPY_FROM_AND_BIND_OUT_PTR` (plus a size
//   annotation).
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
// - side-effect-free `+`, `-`, `*`, `/`, and `c ? a : b` (no math in `c`),
//   checked for overflow, division by zero, and negative values at runtime
//   (`CHECK`-fail). Other operators, such as `%`, `&`, or `<<`, do not compile.
// This is similar to "__counted_by" in Clang's proposed `-fbounds-safety`
// annotations.
//
// For example:
//   void my_memcpy(char* dst SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(n),
//                  const char* src SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(n),
//                  size_t n);
#define SANDBOX_ELEM_SIZED_BY(elem_size_arg)    \
  [[clang::annotate("sandbox", "elem_sized_by", \
                    SANDBOX_INTERNAL_STRINGIFY_NEST(elem_size_arg))]]

// Pointer argument annotation that denotes that the pointee data is an array
// with the size `byte_size_arg` bytes (not number of elements).
// Similar to "__sized_by" in Clang's proposed `-fbounds-safety` annotations.
// The allowed expressions for `byte_size_arg` are the same as for
// SANDBOX_ELEM_SIZED_BY.
//
// For example:
//   void my_memset(void* dst SANDBOX_OUT_PTR SANDBOX_BYTE_SIZED_BY(n),
//                  int c, size_t n);
#define SANDBOX_BYTE_SIZED_BY(byte_size_arg)    \
  [[clang::annotate("sandbox", "byte_sized_by", \
                    SANDBOX_INTERNAL_STRINGIFY_NEST(byte_size_arg))]]

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
#define SANDBOX_ELEM_SIZED_BY_OUTPARAM(size_outparam, capacity_expr) \
  [[clang::annotate("sandbox", "elem_sized_by_outparam",             \
                    SANDBOX_INTERNAL_STRINGIFY_NEST(size_outparam),  \
                    SANDBOX_INTERNAL_STRINGIFY_NEST(capacity_expr))]]

// A pointer argument annotation like SANDBOX_ELEM_SIZED_BY_OUTPARAM, but for
// byte sizes (similar to SANDBOX_BYTE_SIZED_BY).
#define SANDBOX_BYTE_SIZED_BY_OUTPARAM(size_outparam, capacity_expr) \
  [[clang::annotate("sandbox", "byte_sized_by_outparam",             \
                    SANDBOX_INTERNAL_STRINGIFY_NEST(size_outparam),  \
                    SANDBOX_INTERNAL_STRINGIFY_NEST(capacity_expr))]]

// Pointer argument, typedef, and struct field annotation that denotes that the
// pointee data is a null- or zero-sentinel-terminated array:
// - Null-terminated C strings (`const char*`, `char*`).
// - Zero/null-terminated scalar and enum input arrays (`const unsigned char*`,
//   `const uint16_t*`, `const char16_t*`, `const int*`, `const Enum*`), where
//   the array ends at the first `0` element.
// - `nullptr`-terminated pointer input arrays (e.g., `const char* const lst[]`,
//   `const xmlChar**`) when combined with a pointee `typedef` that describes
//   how each non-null element pointer is marshaled.
// - Zero-sentinel-terminated struct input arrays (e.g., `const luaL_Reg*`, or
//   `const struct option*` with null `flag`) where the terminating struct
//   element has all fields equal to zero/`nullptr` (combine with
//   `SANDBOX_ANNOTATE_STRUCT` for input pointer, opaque, or callback fields).
// - `const char*` return values and outparams are also supported, when paired
//   with a lifetime or binding annotation (`SANDBOX_OUT_PTR
//   SANDBOX_LIFETIME_GLOBAL` for static/global strings, or
//   `SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(ctx)` for strings whose lifetime is
//   tied to a context handle `ctx`) to indicate how to manage a host copy.
//
// Input examples:
//   // C string or scalar/enum array:
//   int my_open(const char* path SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED);
//   int sum_ids(const int* ids SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED);
//
//   // nullptr-terminated array of C strings:
//   SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED
//   typedef const char* cstring_elem_t;
//   int check_opt(const cstring_elem_t* lst
//                     SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED);
//
//   // Zero-sentinel-terminated array of structs:
//   SANDBOX_ANNOTATE_STRUCT(struct luaL_Reg {
//     const char* name SANDBOX_NULL_TERMINATED;
//     lua_CFunction func;
//   };)
//   void set_funcs(const struct luaL_Reg* l
//                      SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED);
//
// Output examples:
// as a return value with global lifetime:
//   SANDBOX_OUT_PTR SANDBOX_NULL_TERMINATED SANDBOX_LIFETIME_GLOBAL
//   const char* status_to_error(int error_code);
// as a return value bound to a context handle `ctx`:
//   SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(ctx) SANDBOX_NULL_TERMINATED
//   const char* get_name(Context* ctx SANDBOX_OPAQUE_PTR);
// or, as an outparam:
//   void status_to_error(int error_code,
//                        const char** error_msg SANDBOX_OUT_PTR
//                                               SANDBOX_NULL_TERMINATED
//                                               SANDBOX_LIFETIME_GLOBAL);
#define SANDBOX_NULL_TERMINATED \
  [[clang::annotate("sandbox", "null_terminated")]]

// Pointer annotation that denotes that the pointee data is an array with a
// size (in bytes) that should be looked up from an earlier binding to a
// `context` object (see SANDBOX_BIND_SIZE).
// We allow simple expressions involving binding names prefixed with '$'
// (e.g., simply "$binding_name", or "2 * param * $binding_name").
#define SANDBOX_BYTE_SIZED_BY_BINDING(context, binding_expr)  \
  [[clang::annotate("sandbox", "sized_by_binding",            \
                    SANDBOX_INTERNAL_STRINGIFY_NEST(context), \
                    SANDBOX_INTERNAL_STRINGIFY_NEST(binding_expr))]]

// Wraps a re-definition of a struct. The re-definition allows us to attach
// annotations to each struct data member without patching the original struct
// definition.
//
// NOTE: Syncing trivially copyable structs is supported without this (no
// pointer data members to annotate).
//
// For example:
//   // Original definition (in the library, no annotations):
//   struct Span { ... };
//
//   // Re-definition with annotations:
//   SANDBOX_ANNOTATE_STRUCT(struct Span {
//     char* data SANDBOX_ELEM_SIZED_BY(len);
//     size_t len;
//   };)
//
// One may omit irrelevant struct data members. However, the members that are
// copied over must have the same names and types as in the original struct.
//
// Annotate struct-level invariants that hold across call sites (member sizes,
// `SANDBOX_OPAQUE_PTR`, `SANDBOX_LIFETIME_MALLOC`, or callback typedefs), not
// call-site-specific directions (`SANDBOX_IN_PTR`, `SANDBOX_OUT_PTR`).
//
// Note: The entire `struct Name { ... };` definition (including the `struct`
// keyword and trailing semicolon) must be passed inside the macro parentheses.
#define SANDBOX_ANNOTATE_STRUCT(struct_decl) \
  SANDBOX_ANNOTATE_STRUCT_IMPL(struct_decl)

// Like SANDBOX_BYTE_SIZED_BY_BINDING, but the bound expression specifies the
// number of elements rather than the number of bytes.
#define SANDBOX_ELEM_SIZED_BY_BINDING(context, binding_expr)  \
  [[clang::annotate("sandbox", "elem_sized_by_binding",       \
                    SANDBOX_INTERNAL_STRINGIFY_NEST(context), \
                    SANDBOX_INTERNAL_STRINGIFY_NEST(binding_expr))]]

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

// Indicates that an output pointer (a function return value, a `T**`
// out-parameter, a pointee `typedef`, or a pointer field in an `OUT`/`INOUT`
// struct) points to a heap buffer or null-terminated string newly allocated by
// the sandboxee that the host caller takes ownership of and releases with
// `free()`.
//
// After the sandboxed function returns a non-null pointer, the host wrapper
// allocates a matching buffer via `malloc()` on the host, copies the buffer or
// null-terminated array from the sandboxee into that host buffer, immediately
// frees the sandboxee allocation, and hands the host `malloc()` pointer to the
// caller.
//
// Example (return value):
//   SANDBOX_OUT_PTR SANDBOX_NULL_TERMINATED SANDBOX_LIFETIME_MALLOC
//   char* render_html(const char* md SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED);
//
// Example (out-parameter):
//   int dump_packet(uint8_t** out_buf SANDBOX_OUT_PTR
//                                     SANDBOX_ELEM_SIZED_BY(*out_len)
//                                     SANDBOX_LIFETIME_MALLOC,
//                   size_t* out_len SANDBOX_OUT_PTR);
//
// Example (struct pointer member in an output struct):
//   SANDBOX_ANNOTATE_STRUCT(struct NC_memio {
//     size_t size;
//     void* memory SANDBOX_BYTE_SIZED_BY(size) SANDBOX_LIFETIME_MALLOC;
//   };)
//   int nc_close_memio(int ncid, NC_memio* info SANDBOX_OUT_PTR);
#define SANDBOX_LIFETIME_MALLOC \
  [[clang::annotate("sandbox", "lifetime_malloc")]]

// Indicates that the sandbox borrows a callback-returned buffer for the
// duration of the outer sandboxed function call, and that writes made by the
// sandbox to that buffer are copied back to the host when the outer call
// returns.
//
// Because the host callback allocates and owns the buffer, the sandbox frees
// its local copy when the outer call completes. This is the only supported
// lifetime for callback-returned buffers and must be specified explicitly on
// every pointer-returning callback.
//
// Place this annotation on the callback `typedef` declaration that describes
// the callback, where prefix annotations apply to the callback's return value
// (since C syntax does not allow attributes directly on a function pointer's
// return type).
//
// For example:
//
//   SANDBOX_LIFETIME_OUTER_CALL SANDBOX_ELEM_SIZED_BY(out_size)
//   typedef uint8_t* (*allocate_output_cb)(size_t out_size);
//
//   uint8_t* decode(
//       const uint8_t* src SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(in_size),
//       size_t in_size,
//       allocate_output_cb alloc);
//
// TODO(b/491762076): Support additional callback-return lifetimes (such as
// buffers retained across calls or freed by the sandbox) when a use case
// arises. Requiring an explicit lifetime annotation now ensures that adding
// new lifetime options later does not alter the behavior of existing
// declarations.
#define SANDBOX_LIFETIME_OUTER_CALL \
  [[clang::annotate("sandbox", "lifetime_outer_call")]]

// Indicates that the annotated pointer must alias another pointer (argument
// of the annotation is the name of the other pointer). An exception to the
// "must alias" is that we allow null (in case a function returns null to
// indicate an error).
//
// These alias relations have a few known use cases:
// (1) Returned pointers that alias arguments
// (2) Callback parameter that alias outer function arguments
//
// (1) For returned pointers: this indicates that a returned pointer is an alias
// of an incoming (host-owned) pointer parameter, and thus has the same lifetime
// as that other pointer. Given this lifetime information we do not need to
// allocate and manage memory for the output. Thus, we do not need to know the
// size for array outputs. This annotation implies SANDBOX_OUT_PTR (no need to
// specify both).
//
// This often shows up when functions return an outparam for convenience,
// and the exception for null return values is also common for error handling.
//
// For example:
//
//   SANDBOX_ALIAS_PTR(p)
//   MyStruct* InitStructPartA(MyStruct* p SANDBOX_OUT_PTR)
//
// This is similar to [[clang::lifetimebound]], but we are focused more
// on avoiding memory management than use-after-free.
//
// (2) For callback parameters: this indicates that an "outer" function's
// callback parameter has a parameter that will be an alias of one of the
// outer function's parameters.
//
// For example:
//
//   void process_data(
//       void (*callback)(int,
//                        void* data
//                           SANDBOX_HOST_OPAQUE_PTR
//                           SANDBOX_ALIAS_PTR(closure_data)),
//       void* closure_data SANDBOX_HOST_OPAQUE_PTR);
//
// The caller of `callback` *should* pass along `closure_data` as the second
// argument to `callback`, but the caller is often the sandboxee, and we may
// not want to trust that the sandboxee will pass along the correct pointer.
// The lwbox code generator can emit code to check that the expected aliasing
// really happens or override and pass along the expected pointer anyway.
//
// For now, we do not support aliasing other pairs of pointers, such as:
// - alias a global variable, or
// - alias two callback parameters without aliasing through an outer function
//   parameter.
// - alias a struct member reachable from a pointer parameter.
#define SANDBOX_ALIAS_PTR(param_name) \
  [[clang::annotate("sandbox", "alias_ptr", #param_name)]]

// Indicate that a returned pointer (unless null) is an alias of a callback
// parameter's return value, and thus has the same lifetime. There are some
// subtleties:
//
// - the callback may be called zero, one, or multiple times, and we return:
//   - 0 times: return null
//   - 1 time: return the callback return value
//   - multiple times: return the callback return value that, at execution time,
//     the outer function actually decided to return (rather than guessing
//     it will be the last one).
// - or, the outer function's return value may be null (perhaps for error
//   handling) even if the callback did not return null (no actually aliasing)
//
// Given this annotation, we do not need to *separately* allocate and manage
// memory for the output. For example, we do not need to know the size for array
// outputs, synchronization with SANDBOX_OUT_PTR vs SANDBOX_OPAQUE_PTR, etc..
// However, the callback typedef must describe what it returns.
//
// For example:
//
//   SANDBOX_LIFETIME_OUTER_CALL SANDBOX_ELEM_SIZED_BY(out_size)
//   typedef uint8_t* (*output_allocator)(size_t out_size);
//
//   SANDBOX_ALIAS_CALLBACK_RETURN(allocate_output_cb)
//   uint8_t* Decode(
//       const char* src SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(in_size),
//       size_t in_size,
//       output_allocator allocate_output_cb);
//
// `SANDBOX_LIFETIME_OUTER_CALL` in `output_allocator` specifies the lifetime
// of the callback-returned buffer, while `SANDBOX_ALIAS_CALLBACK_RETURN`
// specifies that `Decode` returns that same buffer rather than a separate
// allocation. Both annotations are required.
#define SANDBOX_ALIAS_CALLBACK_RETURN(cb_param_name) \
  [[clang::annotate("sandbox", "alias_callback_return", #cb_param_name)]]

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
// The bound value can be retrieved, e.g., with SANDBOX_BYTE_SIZED_BY_BINDING.
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
#define SANDBOX_BIND_DATA(context, type, name, host_computable_expr)        \
  [[clang::annotate("sandbox", "bind_data",                                 \
                    SANDBOX_INTERNAL_STRINGIFY_NEST(context), #type, #name, \
                    SANDBOX_INTERNAL_STRINGIFY_NEST(host_computable_expr))]]

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
//
// We also require the pointer to have a size annotation, e.g.
// SANDBOX_BYTE_SIZED_BY, SANDBOX_NULL_TERMINATED, or
// SANDBOX_BYTE_SIZED_BY_BINDING. Supported pointee types are
// primitive/character types and deeply trivially copyable structs (on both
// pointer return values and `T**` outparams).
//
// For example (continuing from the `create_image` of SANDBOX_BIND_DATA):
//
//   SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(context)
//   SANDBOX_BYTE_SIZED_BY_BINDING(context, "$size")
//   char* get_image_buffer(Image* context SANDBOX_OPAQUE_PTR);
#define SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(context)           \
  [[clang::annotate("sandbox", "copy_from_and_bind_out_ptr",  \
                    SANDBOX_INTERNAL_STRINGIFY_NEST(context), \
                    SANDBOX_BINDING_GEN_NAME())]]

// Pointer argument annotation that denotes that the pointee data (which is
// host-owned) should be retained with a lifetime bound to a context object.
// Under the hood, we will allocate a sandbox copy to give the sandbox access
// to the host data. The sandbox copy will be destroyed when calling a function
// annotated with SANDBOX_CLEAR_BINDINGS (the host copy is still owned by the
// host).
//
// If the pointer is null, then we do not allocate and bind, and leave it as
// null in the sandbox. We also skip if the context is null.
//
// - context: the pointer to the object to bind the sandbox copy to
//   (can be SANDBOX_RETURN if the context is the return value).
//
// We also require the pointer to have a size annotation.
//
// A pointer direction annotation (IN/OUT/INOUT) will specify whether data is
// copied in or out for the call to the function with this annotation.
//
// For example, suppose we have a version of `create_image` that takes the
// compressed data from the host and consults that data to progressively
// decompress each row of the image, and so needs to retain the input buffer:
//
//   SANDBOX_OPAQUE_PTR
//   Image* create_image(char* input_data
//                           SANDBOX_IN_PTR
//                           SANDBOX_RETAIN_AND_BIND(/*context=*/SANDBOX_RETURN)
//                           SANDBOX_BYTE_SIZED_BY(size),
//                       size_t size);
#define SANDBOX_RETAIN_AND_BIND(context)                      \
  [[clang::annotate("sandbox", "retain_and_bind",             \
                    SANDBOX_INTERNAL_STRINGIFY_NEST(context), \
                    SANDBOX_BINDING_GEN_NAME())]]

// An annotation for a pointer parameter (e.g., a context object), that denotes
// that this function will destroy/free the pointee. We use this as a hook to
// also clear all bindings attached to the given pointee, and free any bound
// copies, if necessary.
// This assumes that the order of freeing doesn't matter.
// If the context is null, then there should be no bindings to clear.
//
// Note: Bound buffers are keyed by the sandbox context pointer address, so a
// function annotated with `SANDBOX_CLEAR_BINDINGS` must be called on that
// handle before the sandbox frees it (functions that transfer handle or host
// buffer ownership to the sandbox to free internally are not supported).
//
// For example:
//
//   void destroy_image(Image* context SANDBOX_OPAQUE_PTR
//                                     SANDBOX_CLEAR_BINDINGS);
#define SANDBOX_CLEAR_BINDINGS [[clang::annotate("sandbox", "clear_bindings")]]

/////////////////////////////////////////////////////////////////////////////
// Annotations for deeply syncing non-trivially copyable structures.
//
// Specify access paths from the struct pointer parameter to any pointer fields
// that need deep syncing. All unspecified pointers are copied shallowly.
//
// Current limitations:
// - Only single-layer dereferencing is supported in access paths.
// - Pointers are assumed to be host-allocated and non-aliasing.
//
// Access paths are grouped with context-sensitive annotations (direction,
// lifetime, or binding) inside `{ ... }`, separated by commas.
// Note: Sized-by annotations are invariants specified separately via
// `SANDBOX_ANNOTATE_STRUCT`.
//
// Example 1 -- borrowing a temporary reference to a host buffer:
//
//   struct IntVector {
//     int* data;
//     size_t len;
//   };
//
//   // Annotate that `data` is an array (not just one elem).
//   SANDBOX_ANNOTATE_STRUCT(struct IntVector {
//     char* data SANDBOX_ELEM_SIZED_BY(len);
//     size_t len;
//   };)
//
//   // Sums the ints in `vec`, modifying them in place.
//   int sum_and_inc(IntVector* vec
//                      SANDBOX_IN_PTR
//                      SANDBOX_STRUCT_SYNC({vec->data, inout_ptr}));
//
//   (SANDBOX_IN_PTR describes the top-level `vec` pointer itself, while
//    `vec->data` is an `inout_ptr` indicating its data is synced in and out.)
//
// Example 2 -- retaining and binding a host buffer to a context object:
//
//   struct CharSpan {
//     char* data;
//     size_t len;
//   };
//
//   // Creates a decoder using the buffer in `span`. The buffer must remain
//   // valid and unchanged for the lifetime of the returned decoder.
//   SANDBOX_OPAQUE_PTR
//   Decoder* create_decoder(
//       CharSpan* span
//          SANDBOX_IN_PTR
//          SANDBOX_STRUCT_SYNC({span->data, in_ptr,
//                               retain_and_bind(SANDBOX_RETURN)}));
#define SANDBOX_STRUCT_SYNC(...)                                          \
  [[clang::annotate("sandbox", "struct_sync", SANDBOX_BINDING_GEN_NAME(), \
                    SANDBOX_STRUCT_SYNC_ACCESS_PATHS(__VA_ARGS__) "$")]]
// Note: "$" is just to terminate the list of access paths and avoid a
// trailing comma in the list of attribute arguments.

// Indicate that we only need a shallow copy of the pointee data.
// This is useful for complex structs with pointers, when the function does not
// touch the deeper pointers. Like SANDBOX_STRUCT_SYNC, but with an empty list.
//
// For example:
//
//   // Initializes the primitive "options" data members in `out` to reasonable
//   // default values. Does not allocate or fill deeper members of
//   // `out` like buffers.
//   void init_output_options(Output* out SANDBOX_OUT_PTR SANDBOX_SHALLOW_SYNC);
#define SANDBOX_SHALLOW_SYNC \
  [[clang::annotate("sandbox", "shallow_struct_sync")]]

// Pointer annotation that denotes that the pointee holds uninitialized memory.
// The host or sandbox will have the space allocated, but we do not need to
// copy in/out any initial values, and the system can initialize to an
// arbitrary value.
// Whether this comes from an allocator or needs to be freed in a certain
// way is determined by other annotations. In most cases, this property can
// be better conveyed by pointer direction annotations, e.g.,
// SANDBOX_OUT_PTR instead of SANDBOX_INOUT_PTR avoids input initialization.
// This is only needed when the pointer direction is not sufficient.
//
// For example, for output pointers:
//
//   SANDBOX_OUT_PTR SANDBOX_ELEM_SIZED_BY(num_ints) SANDBOX_LIFETIME_MALLOC
//   int* my_calloc_ints(size_t num_ints, int default_value);
// vs
//   SANDBOX_OUT_PTR SANDBOX_UNINITIALIZED
//   SANDBOX_ELEM_SIZED_BY(num_ints) SANDBOX_LIFETIME_MALLOC
//   int* my_malloc_ints(size_t num_ints);
//
// For `my_calloc_ints` we will copy out the initial `default_value` values,
// but for `my_malloc_ints` we do not need to copy out any initial values
// as the caller will likely overwrite/initialize.
#define SANDBOX_UNINITIALIZED [[clang::annotate("sandbox", "uninitialized")]]

// Marks a callback (either on a callback parameter or on a callback typedef
// declaration) as safe to be invoked multiple times by the sandboxee during a
// call or across retained calls.
//
// Without this annotation, host callbacks default to single-shot ("at most
// once per host registration/call"): the generated host wrapper tombstones the
// callback after its first invocation and CHECK-fails if the sandboxee
// attempts to invoke it a second time without the host re-arming it.
#define SANDBOX_CALLBACK_IDEMPOTENT \
  [[clang::annotate("sandbox", "callback_idempotent")]]

// Multi-level pointer parameters (`T**`, `T***`) are annotated by defining a
// pointer `typedef` for each inner pointer level (`*p`, `p[i]`, `**p`, etc.)
// and attaching that level's direction, bounds, and lifetime annotations to
// the `typedef`. Multi-level pointers of depth 3 or more (`T***`) chain
// pointer `typedef`s.
//
// Example (out-parameter returning an opaque handle, `DbHandle**`):
//   typedef DbHandle* db_handle_pointee SANDBOX_OPAQUE_PTR;
//   int open_db(const char* path SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED,
//               db_handle_pointee* out_db SANDBOX_OUT_PTR);
//
// Example (3-level pointer, `int***`):
//   typedef int* row_pointee SANDBOX_INOUT_PTR SANDBOX_ELEM_SIZED_BY(cols);
//   typedef row_pointee* matrix_pointee SANDBOX_INOUT_PTR
//       SANDBOX_ELEM_SIZED_BY(rows);
//   void update_matrix_handle(
//       matrix_pointee* matrix_out SANDBOX_INOUT_PTR,
//       size_t rows, size_t cols);

#endif  // SANDBOXED_API_ANNOTATIONS_H_
