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

#include "sandboxed_api/tests/testcases/replaced_library_reverse_callbacks.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <atomic>

#include "absl/base/attributes.h"

namespace {

int AddInts(int a, int b) { return a + b; }

int MultiplyInts(int a, int b) { return a * b; }

int SumBytes(const uint8_t* buf, size_t len) {
  int sum = 0;
  for (size_t i = 0; i < len; ++i) {
    sum += buf[i];
  }
  return sum;
}

std::atomic<int (*)(int, int)> g_hook{nullptr};

CalculatorHooks g_struct_hooks = {nullptr, 0};

int g_sandbox_processed_acc = 0;
void (*g_registered_process_cbs[4])(const char* buf, size_t size) = {
    nullptr, nullptr, nullptr, nullptr};

void PrivDefaultProcessBuffer(const char* buf, size_t size) {
  if (buf == nullptr) return;
  for (size_t i = 0; i < size; ++i) {
    g_sandbox_processed_acc += static_cast<unsigned char>(buf[i]);
  }
}

void PrivDefaultUnusedCallback(const char* cstr) {
  if (cstr == nullptr) return;
  for (const char* p = cstr; *p != '\0'; ++p) {
    g_sandbox_processed_acc += static_cast<unsigned char>(*p);
  }
}

void DefaultXmlElementCallback(void* /*ctx*/, const xml_char_t* name) {
  if (name == nullptr) return;
  for (const xml_char_t* p = name; *p != 0; ++p) {
    g_sandbox_processed_acc += static_cast<int>(*p);
  }
}

int BuiltinLuaNegate(int arg) { return -arg; }

int BuiltinLuaSquare(int arg) { return arg * arg; }

}  // namespace

extern "C" {

uintptr_t get_adder(int which) {
  switch (which) {
    case 0:
      return reinterpret_cast<uintptr_t>(&AddInts);
    case 1:
      return reinterpret_cast<uintptr_t>(&MultiplyInts);
    default:
      return 0;
  }
}

ABSL_ATTRIBUTE_NO_SANITIZE_CFI int apply_twice(int (*cb)(int, int), int a,
                                               int b) {
  if (cb == nullptr) {
    return 0;
  }
  return cb(cb(a, b), b);
}

void install_hook(int (*cb)(int, int)) {
  g_hook.store(cb, std::memory_order_release);
}

ABSL_ATTRIBUTE_NO_SANITIZE_CFI int run_hook(int a, int b) {
  int (*cb)(int, int) = g_hook.load(std::memory_order_acquire);
  if (cb == nullptr) {
    return 0;
  }
  return cb(a, b);
}

int (*get_hook())(int, int) { return g_hook.load(std::memory_order_acquire); }

int (*get_adder_fn(int which))(int, int) {
  switch (which) {
    case 0:
      return &AddInts;
    case 1:
      return &MultiplyInts;
    default:
      return nullptr;
  }
}

void init_default_hooks(struct CalculatorHooks* hooks, int which) {
  if (hooks == nullptr) {
    return;
  }
  hooks->combine = get_adder_fn(which);
  hooks->initial_value = 10;
}

ABSL_ATTRIBUTE_NO_SANITIZE_CFI int run_with_hooks(
    const struct CalculatorHooks* hooks, int a, int b) {
  if (hooks == nullptr) {
    return 0;
  }
  if (hooks->combine == nullptr) {
    return hooks->initial_value;
  }
  return hooks->combine(hooks->initial_value + a, b);
}

void install_struct_hooks(struct CalculatorHooks* hooks) {
  if (hooks == nullptr) {
    return;
  }
  g_struct_hooks = *hooks;
  hooks->initial_value += 1;
}

int run_installed_struct_hooks(int a, int b) {
  return run_with_hooks(&g_struct_hooks, a, b);
}

void init_default_buffer_processor(struct BufferProcessor* proc) {
  if (proc == nullptr) {
    return;
  }
  proc->process_bytes = &SumBytes;
  proc->scale = 2;
}

ABSL_ATTRIBUTE_NO_SANITIZE_CFI int run_buffer_processor(
    const struct BufferProcessor* proc, const uint8_t* data, size_t len) {
  if (proc == nullptr || proc->process_bytes == nullptr) {
    return 0;
  }
  return proc->process_bytes(data, len) * proc->scale;
}

void init_jpeg_style_callbacks(struct JpegStyleCallbacks* s) {
  if (s == nullptr) {
    return;
  }
  s->process_buffer = &PrivDefaultProcessBuffer;
  s->unused_callback = &PrivDefaultUnusedCallback;
}

ABSL_ATTRIBUTE_NO_SANITIZE_CFI void call_process_buffs(
    const struct JpegStyleCallbacks* s) {
  if (s == nullptr || s->process_buffer == nullptr) {
    return;
  }
  const char kInternalBuf[] = "\x01\x02\x03\x04";
  s->process_buffer(kInternalBuf, 4);
}

ABSL_ATTRIBUTE_NO_SANITIZE_CFI void call_process_buffer_callback(
    void (*cb)(const char* buf, size_t size), const char* buf, size_t size) {
  if (cb == nullptr) {
    return;
  }
  cb(buf, size);
}

void register_process_buffer_callback(int slot, void (*cb)(const char* buf,
                                                           size_t size)) {
  if (slot < 0 || slot >= 4) {
    return;
  }
  g_registered_process_cbs[slot] = cb;
}

void (*get_registered_process_buffer_callback(int slot))(const char* buf,
                                                         size_t size) {
  if (slot < 0 || slot >= 4) {
    return nullptr;
  }
  return g_registered_process_cbs[slot];
}

int get_sandbox_processed_acc() { return g_sandbox_processed_acc; }

void reset_sandbox_processed_acc() { g_sandbox_processed_acc = 0; }

void init_nested_jpeg_pipeline(struct NestedJpegPipeline* p, int id) {
  if (p == nullptr) {
    return;
  }
  init_jpeg_style_callbacks(&p->callbacks);
  p->pipeline_id = id;
}

ABSL_ATTRIBUTE_NO_SANITIZE_CFI int run_nested_jpeg_pipeline(
    const struct NestedJpegPipeline* p) {
  if (p == nullptr) {
    return -1;
  }
  call_process_buffs(&p->callbacks);
  if (p->callbacks.unused_callback != nullptr) {
    p->callbacks.unused_callback("OK");
  }
  return p->pipeline_id;
}

int is_reverse_cb_marshaled_copy(const int* probe, uintptr_t caller_addr) {
  return reinterpret_cast<uintptr_t>(probe) != caller_addr ? 1 : 0;
}

ABSL_ATTRIBUTE_NO_SANITIZE_CFI int invoke_single_shot_callback_n_times(
    int times, int (*cb)(int x)) {
  if (cb == nullptr) {
    return 0;
  }
  int sum = 0;
  for (int i = 0; i < times; ++i) {
    sum += cb(i + 1);
  }
  return sum;
}

unary_b_cb swap_callback_typedef_trampoline(unary_a_cb cb) {
  return reinterpret_cast<unary_b_cb>(cb);
}

int xml_utf8_byte_sum(const xml_char_t* str) {
  if (str == nullptr) {
    return -1;
  }
  int sum = 0;
  for (const xml_char_t* p = str; *p != 0; ++p) {
    sum += static_cast<int>(*p);
  }
  return sum;
}

ABSL_ATTRIBUTE_NO_SANITIZE_CFI void dispatch_xml_sax_element(
    void* ctx, void (*on_element)(void* ctx, const xml_char_t* name),
    const xml_char_t* doc_tag) {
  if (on_element == nullptr) {
    return;
  }
  on_element(ctx, doc_tag);
  const xml_char_t kChildTag[] = {'i', 't', 'e', 'm', 0};
  on_element(ctx, kChildTag);
}

void (*get_default_xml_element_cb())(void* ctx, const xml_char_t* name) {
  return &DefaultXmlElementCallback;
}

ABSL_ATTRIBUTE_NO_SANITIZE_CFI int utf16_code_unit_sum(
    const uint16_t* utf16_str, int (*on_utf16)(const uint16_t* segment)) {
  if (utf16_str == nullptr) {
    return -1;
  }
  int sum = 0;
  for (const uint16_t* p = utf16_str; *p != 0; ++p) {
    sum += static_cast<int>(*p);
  }
  if (on_utf16 != nullptr) {
    const uint16_t kSuffix[] = {0x003B, 0x03A9, 0x0000};
    sum += on_utf16(kSuffix);
  }
  return sum;
}

void mutate_or_clear_utf16_packet(struct Utf16MutablePacket* pkt) {
  if (pkt == nullptr) {
    return;
  }
  if (pkt->mode == 1) {
    pkt->text16 = nullptr;
    return;
  }
  if (pkt->text16 == nullptr) {
    return;
  }
  if (pkt->mode == 2) {
    size_t i = 0;
    while (pkt->text16[i] != 0) {
      pkt->text16[i] = 'Z';
      ++i;
    }
    pkt->text16[i] = 0xBEEF;
    return;
  }
  for (size_t i = 0; pkt->text16[i] != 0; ++i) {
    pkt->text16[i] += 1;
  }
}

struct XmlFilterCtx {
  XmlFilterOp ops[4];
  uint16_t errmsg16[4];
};

XmlFilterCtx* create_xml_filter_ctx(int empty) {
  auto* ctx = new XmlFilterCtx();
  if (empty) {
    ctx->ops[0] = kXmlOpEnd;
    ctx->errmsg16[0] = 0;
  } else {
    ctx->ops[0] = kXmlOpAddTen;
    ctx->ops[1] = kXmlOpDouble;
    ctx->ops[2] = kXmlOpAddLen;
    ctx->ops[3] = kXmlOpEnd;
    ctx->errmsg16[0] = 'E';
    ctx->errmsg16[1] = 0x03A9;
    ctx->errmsg16[2] = '!';
    ctx->errmsg16[3] = 0;
  }
  return ctx;
}

const enum XmlFilterOp* get_xml_filter_ctx_ops(XmlFilterCtx* ctx) {
  return ctx != nullptr ? ctx->ops : nullptr;
}

const uint16_t* get_xml_filter_ctx_errmsg16(XmlFilterCtx* ctx) {
  return ctx != nullptr ? ctx->errmsg16 : nullptr;
}

void destroy_xml_filter_ctx(XmlFilterCtx* ctx) { delete ctx; }

int run_xml_filter_ops(const enum XmlFilterOp* ops, const xml_char_t* str) {
  if (ops == nullptr) {
    return -1;
  }
  int len = 0;
  if (str != nullptr) {
    while (str[len] != 0) {
      ++len;
    }
  }
  int acc = 0;
  for (size_t i = 0; ops[i] != kXmlOpEnd; ++i) {
    switch (ops[i]) {
      case kXmlOpAddLen:
        acc += len;
        break;
      case kXmlOpDouble:
        acc *= 2;
        break;
      case kXmlOpAddTen:
        acc += 10;
        break;
      case kXmlOpEnd:
        break;
    }
  }
  return acc;
}

int (*get_builtin_lua_cfunc(int which))(int arg) {
  switch (which) {
    case 0:
      return &BuiltinLuaNegate;
    case 1:
      return &BuiltinLuaSquare;
    default:
      return nullptr;
  }
}

ABSL_ATTRIBUTE_NO_SANITIZE_CFI int lua_setfuncs_and_dispatch(
    const struct LuaRegEntry* l, const char* target_name, int arg,
    int* out_total_entries) {
  if (l == nullptr) {
    return -1;
  }
  int count = 0;
  int result = -1;
  for (size_t i = 0; l[i].name != nullptr || l[i].func != nullptr; ++i) {
    ++count;
    if (target_name != nullptr && l[i].name != nullptr &&
        strcmp(l[i].name, target_name) == 0) {
      result = l[i].func != nullptr ? l[i].func(arg) : 0;
    }
  }
  if (out_total_entries != nullptr) {
    *out_total_entries = count;
  }
  return result;
}

int match_long_option_spec(const struct LongOptionSpec* longopts,
                           const char* arg_name, int* out_has_arg) {
  if (longopts == nullptr || arg_name == nullptr) {
    return -1;
  }
  for (size_t i = 0; longopts[i].name != nullptr || longopts[i].has_arg != 0 ||
                     longopts[i].val != 0;
       ++i) {
    if (longopts[i].name != nullptr &&
        strcmp(longopts[i].name, arg_name) == 0) {
      if (out_has_arg != nullptr) {
        *out_has_arg = longopts[i].has_arg;
      }
      return longopts[i].val;
    }
  }
  return -1;
}

ABSL_ATTRIBUTE_NO_SANITIZE_CFI int eval_key_value_pairs(
    const struct KeyValuePair* pairs,
    int (*on_pairs)(const struct KeyValuePair* pairs)) {
  if (pairs == nullptr) {
    return -1;
  }
  int sum = 0;
  for (size_t i = 0;
       pairs[i].key != 0 || pairs[i].value != 0 || pairs[i].tag != 0; ++i) {
    sum += pairs[i].key * pairs[i].value;
  }
  if (on_pairs != nullptr) {
    struct KeyValuePair internal_pairs[4];
    // Fill the backing storage with `0xAB` so that the 3 trailing padding
    // bytes of the `{0, 0, 0}` sentinel (`internal_pairs[2]`) are not zero, as
    // can happen in real callers. `lwbox_runtime_test.cc` checks that sentinel
    // detection ignores these bytes.
    memset(internal_pairs, 0xAB, sizeof(internal_pairs));
    internal_pairs[0].key = 2;
    internal_pairs[0].value = 10;
    internal_pairs[0].tag = 'a';
    internal_pairs[1].key = 3;
    internal_pairs[1].value = 20;
    internal_pairs[1].tag = 'b';
    internal_pairs[2].key = 0;
    internal_pairs[2].value = 0;
    internal_pairs[2].tag = 0;
    internal_pairs[3].key = 999;
    internal_pairs[3].value = 999;
    internal_pairs[3].tag = 'z';
    sum += on_pairs(internal_pairs);
  }
  return sum;
}

}  // extern "C"
