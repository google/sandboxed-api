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

#include <stddef.h>
#include <stdint.h>

#include "sandboxed_api/annotations.h"
#include "sandboxed_api/tests/testcases/replaced_library_reverse_callbacks.h"

// None of these signatures needs an annotation on its own account: every
// argument is a scalar, and `apply_twice`'s callback neither takes nor returns
// a pointer. The callback typedef below is there for the generated machinery it
// reaches -- using a callback typedef is what makes the generator emit that
// typedef's sandboxee invoker and its pool of host thunks.

extern "C" {

// What `apply_twice` calls. Nothing in it needs describing beyond its
// existence, which is what makes it the smallest case the reverse direction
// can be built against. Marked `SANDBOX_CALLBACK_IDEMPOTENT` because
// `apply_twice` invokes `cb(cb(a, b), b)` twice within a single call and
// `install_hook` + `run_hook` invokes it across multiple calls.
SANDBOX_CALLBACK_IDEMPOTENT typedef int (*adder)(int a, int b);

typedef int (*byte_summer)(
    const uint8_t* buf SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(len), size_t len);

SANDBOX_ANNOTATE_STRUCT(struct CalculatorHooks { adder combine; };)

SANDBOX_ANNOTATE_STRUCT(struct BufferProcessor { byte_summer process_bytes; };)

uintptr_t get_adder(int which);

int apply_twice(adder cb, int a, int b);

// The same callback typedef again, on a callback the library keeps rather than
// uses and forgets.
void install_hook(adder cb);

int run_hook(int a, int b);

adder get_hook();

// The same callback typedef, naming what comes *out*.
//
// Nothing distinguishes this use of `adder` from the one above, which is the
// point: a callback typedef says what a callback means, not which way it
// travels.
adder get_adder_fn(int which);

void init_default_hooks(struct CalculatorHooks* hooks SANDBOX_OUT_PTR,
                        int which);

int run_with_hooks(const struct CalculatorHooks* hooks SANDBOX_IN_PTR, int a,
                   int b);

void install_struct_hooks(struct CalculatorHooks* hooks SANDBOX_INOUT_PTR);

int run_installed_struct_hooks(int a, int b);

void init_default_buffer_processor(
    struct BufferProcessor* proc SANDBOX_OUT_PTR);

int run_buffer_processor(
    const struct BufferProcessor* proc SANDBOX_IN_PTR,
    const uint8_t* data SANDBOX_IN_PTR SANDBOX_ELEM_SIZED_BY(len), size_t len);

SANDBOX_CALLBACK_IDEMPOTENT typedef void (*process_buf_cb)(
    const char* buf SANDBOX_IN_PTR SANDBOX_BYTE_SIZED_BY(size), size_t size);

typedef void (*cstr_cb)(
    const char* cstr SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED);

SANDBOX_ANNOTATE_STRUCT(struct JpegStyleCallbacks {
  process_buf_cb process_buffer;
  cstr_cb unused_callback;
};)

void init_jpeg_style_callbacks(struct JpegStyleCallbacks* s SANDBOX_OUT_PTR);

void call_process_buffs(const struct JpegStyleCallbacks* s SANDBOX_IN_PTR);

void call_process_buffer_callback(
    process_buf_cb cb,
    const char* buf SANDBOX_IN_PTR SANDBOX_BYTE_SIZED_BY(size), size_t size);

void register_process_buffer_callback(int slot, process_buf_cb cb);

process_buf_cb get_registered_process_buffer_callback(int slot);

int get_sandbox_processed_acc();
void reset_sandbox_processed_acc();

void init_nested_jpeg_pipeline(struct NestedJpegPipeline* p SANDBOX_OUT_PTR,
                               int id);
int run_nested_jpeg_pipeline(const struct NestedJpegPipeline* p SANDBOX_IN_PTR);

int is_reverse_cb_marshaled_copy(const int* probe SANDBOX_IN_PTR,
                                 uintptr_t caller_addr);
int invoke_single_shot_callback_n_times(int times, int (*cb)(int x));

typedef int (*unary_a_cb)(int x);
typedef int (*unary_b_cb)(int x);
unary_b_cb swap_callback_typedef_trampoline(unary_a_cb cb);

SANDBOX_CALLBACK_IDEMPOTENT typedef void (*xml_element_cb)(
    void* ctx SANDBOX_HOST_OPAQUE_PTR,
    const xml_char_t* name SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED);

typedef int (*utf16_segment_cb)(
    const uint16_t* segment SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED);

int xml_utf8_byte_sum(
    const xml_char_t* str SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED);

void dispatch_xml_sax_element(
    void* ctx SANDBOX_HOST_OPAQUE_PTR, xml_element_cb on_element,
    const xml_char_t* doc_tag SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED);

xml_element_cb get_default_xml_element_cb();

int utf16_code_unit_sum(const uint16_t* utf16_str SANDBOX_IN_PTR
                            SANDBOX_NULL_TERMINATED,
                        utf16_segment_cb on_utf16);

SANDBOX_ANNOTATE_STRUCT(struct Utf16MutablePacket {
  uint16_t* text16 SANDBOX_NULL_TERMINATED;
};)

void mutate_or_clear_utf16_packet(
    struct Utf16MutablePacket* pkt SANDBOX_INOUT_PTR
        SANDBOX_STRUCT_SYNC({pkt->text16, inout_ptr}));

SANDBOX_OPAQUE_PTR
struct XmlFilterCtx* create_xml_filter_ctx(int empty);

SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(ctx)
SANDBOX_NULL_TERMINATED
const enum XmlFilterOp* get_xml_filter_ctx_ops(
    struct XmlFilterCtx* ctx SANDBOX_OPAQUE_PTR);

SANDBOX_COPY_FROM_AND_BIND_OUT_PTR(ctx)
SANDBOX_NULL_TERMINATED
const uint16_t* get_xml_filter_ctx_errmsg16(
    struct XmlFilterCtx* ctx SANDBOX_OPAQUE_PTR);

void destroy_xml_filter_ctx(
    struct XmlFilterCtx* ctx SANDBOX_OPAQUE_PTR SANDBOX_CLEAR_BINDINGS);

int run_xml_filter_ops(
    const enum XmlFilterOp* ops SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED,
    const xml_char_t* str SANDBOX_IN_PTR SANDBOX_NULL_TERMINATED);

}  // extern "C"
