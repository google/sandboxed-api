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

}  // extern "C"
