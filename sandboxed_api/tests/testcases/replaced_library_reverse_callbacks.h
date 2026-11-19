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

#ifndef SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_REVERSE_CALLBACKS_H_
#define SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_REVERSE_CALLBACKS_H_

#include <stddef.h>
#include <stdint.h>

// A library used to test callbacks in both directions:
// - Forward callbacks implemented by the host and invoked or retained by the
//   sandboxed library.
// - Reverse callbacks implemented by the sandboxed library and returned to the
//   host or stored in struct fields.

extern "C" {

// Hands back the address of one of this library's own functions. 0 for an
// index nobody implements.
//
// A raw address is not a shape any real library should have, and not the one
// the reverse direction will be built around. This exists so that the tests
// for the thunk pool can drive its binding entry point directly, which needs
// a sandboxee address that has not already been turned into a host-callable
// one.
uintptr_t get_adder(int which);

// The forward direction, and the vehicle for the interesting case: calls
// `cb(cb(a, b), b)`, so the callback is genuinely reentered.
int apply_twice(int (*cb)(int, int), int a, int b);

// Stores `cb` for later. This is the shape a struct callback field has with
// the struct taken away: the library keeps the callback past the call that
// handed it over, and calls it from a later one.
void install_hook(int (*cb)(int, int));

// Calls whatever `install_hook` last stored. 0 if nothing was stored.
int run_hook(int a, int b);

// Hands back whatever `install_hook` last stored (the lua_gethook / lua_sethook
// pair).
int (*get_hook())(int, int);

// The same function `get_adder` hands back, but as a callback rather than as
// an address. This is the shape `lua_gethook` has: a library handing back one
// of its own functions for the caller to hold and call. Null for an index
// nobody implements.
int (*get_adder_fn(int which))(int, int);

struct CalculatorHooks {
  int (*combine)(int a, int b);
  int initial_value;
};

// Populates `hooks->combine` with one of the library's own functions (0 -> add,
// 1 -> multiply, anything else -> nullptr) and sets `hooks->initial_value` to
// 10.
void init_default_hooks(struct CalculatorHooks* hooks, int which);

// Calls `hooks->combine(hooks->initial_value + a, b)`, or returns
// `hooks->initial_value` if `hooks->combine` is null.
int run_with_hooks(const struct CalculatorHooks* hooks, int a, int b);

// Stores `*hooks` inside the library for `run_installed_struct_hooks` and
// increments `hooks->initial_value` in place while leaving `hooks->combine`
// untouched.
void install_struct_hooks(struct CalculatorHooks* hooks);

// Runs the `CalculatorHooks` last stored by `install_struct_hooks`.
int run_installed_struct_hooks(int a, int b);

struct BufferProcessor {
  int (*process_bytes)(const uint8_t* buf, size_t len);
  int scale;
};

// Populates `proc->process_bytes` with the library's own byte-summing callback
// and sets `proc->scale` to 2.
void init_default_buffer_processor(struct BufferProcessor* proc);

// Calls `proc->process_bytes(data, len) * proc->scale`.
int run_buffer_processor(const struct BufferProcessor* proc,
                         const uint8_t* data, size_t len);

// Multi-field handler table modeled on C libraries whose init routines
// populate a struct with private default callbacks that the caller may inspect,
// invoke directly, selectively override with host functions, or pass back into
// the library:
// - `libjpeg_turbo`: `jpeg_std_error(&jerr)` / `jpeg_CreateDecompress` fills
//   `jpeg_error_mgr` (`error_exit`, `output_message`, `emit_message`) with
//   internal default routines (`src/jerror.c`), after which callers (e.g.
//   `image/codec/jpegdecoder.cc`) override `error_exit` with a host handler
//   while leaving or calling `output_message` directly.
// - `libxml2` SAX2: `xmlSAX2InitDefaultSAXHandler(&hdlr, ...)` populates a
//   `xmlSAXHandler` table with `libxml2`'s default callbacks (`src/SAX2.c`),
//   and callers (e.g. `bigtable/contrib/btv/btv_xml.cc`) override specific
//   callbacks while forwarding directly through the default SAX callbacks.
// - `libwebp`: `WebPPictureInit(&pic)` sets `pic.writer` to a default internal
//   writer (`src/enc/picture_enc.c`) before callers
//   (`image/codec/webpencoder.cc`) optionally replace it with a custom writer
//   and pass `&pic` to `WebPEncode`.
struct JpegStyleCallbacks {
  void (*process_buffer)(const char* buf, size_t size);
  void (*unused_callback)(const char* cstr);
};

struct NestedJpegPipeline {
  struct JpegStyleCallbacks callbacks;
  int pipeline_id;
};

void init_jpeg_style_callbacks(struct JpegStyleCallbacks* s);
void call_process_buffs(const struct JpegStyleCallbacks* s);
void call_process_buffer_callback(void (*cb)(const char* buf, size_t size),
                                  const char* buf, size_t size);
void register_process_buffer_callback(int slot,
                                      void (*cb)(const char* buf, size_t size));
void (*get_registered_process_buffer_callback(int slot))(const char* buf,
                                                         size_t size);
int get_sandbox_processed_acc();
void reset_sandbox_processed_acc();

void init_nested_jpeg_pipeline(struct NestedJpegPipeline* p, int id);
int run_nested_jpeg_pipeline(const struct NestedJpegPipeline* p);

int is_reverse_cb_marshaled_copy(const int* probe, uintptr_t caller_addr);
int invoke_single_shot_callback_n_times(int times, int (*cb)(int x));

typedef int (*unary_a_cb)(int x);
typedef int (*unary_b_cb)(int x);
unary_b_cb swap_callback_typedef_trampoline(unary_a_cb cb);

// Non-`char` null-terminated scalar and enum sequences modeled on:
// - `libxml2`: `typedef unsigned char xmlChar;` passed to `xmlStrlen`,
//   `xmlCheckUTF8`, and SAX callbacks (`startElementSAXFunc(void* ctx,
//   const xmlChar* name, ...)`).
// - SQLite: UTF-16 null-terminated strings in `sqlite3_open16(const void*, ..)`
//   and `sqlite3_complete16(const void* sql)` (`const uint16_t*` /
//   `const char16_t*`).
// - Zero-terminated integer / enum opcode sequences (`0` / `kXmlOpEnd`
//   sentinel).
typedef unsigned char xml_char_t;

enum XmlFilterOp {
  kXmlOpEnd = 0,
  kXmlOpAddLen = 1,
  kXmlOpDouble = 2,
  kXmlOpAddTen = 3,
};

int xml_utf8_byte_sum(const xml_char_t* str);

void dispatch_xml_sax_element(void* ctx,
                              void (*on_element)(void* ctx,
                                                 const xml_char_t* name),
                              const xml_char_t* doc_tag);

void (*get_default_xml_element_cb())(void* ctx, const xml_char_t* name);

int utf16_code_unit_sum(const uint16_t* utf16_str,
                        int (*on_utf16)(const uint16_t* segment));

struct Utf16MutablePacket {
  uint16_t* text16;
  int mode;
};
void mutate_or_clear_utf16_packet(struct Utf16MutablePacket* pkt);

struct XmlFilterCtx;
struct XmlFilterCtx* create_xml_filter_ctx(int empty);
const enum XmlFilterOp* get_xml_filter_ctx_ops(struct XmlFilterCtx* ctx);
const uint16_t* get_xml_filter_ctx_errmsg16(struct XmlFilterCtx* ctx);
void destroy_xml_filter_ctx(struct XmlFilterCtx* ctx);

int run_xml_filter_ops(const enum XmlFilterOp* ops, const xml_char_t* str);
}  // extern "C"

#endif  // SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_REVERSE_CALLBACKS_H_
