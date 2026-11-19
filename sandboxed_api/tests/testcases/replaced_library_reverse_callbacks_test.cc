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

#include <thread>  // NOLINT(build/c++11)

#include "gtest/gtest.h"

// End-to-end tests for callbacks in both directions against
// `replaced_library_reverse_callbacks`.

namespace {

TEST(ForwardCallback, HostFunctionIsCalledByTheLibrary) {
  // `apply_twice` calls `cb(cb(a, b), b)`, so a callback that is genuinely
  // reentered is the baseline the reverse direction has to match.
  auto host_add = [](int a, int b) { return a + b; };
  EXPECT_EQ(apply_twice(host_add, 2, 3), 8);

  auto host_multiply = [](int a, int b) { return a * b; };
  EXPECT_EQ(apply_twice(host_multiply, 2, 3), 18);
}

TEST(SandboxeeFunctionAddress, IsStableAndDistinct) {
  // `get_adder` is a test probe, not a shape any library should have: it
  // hands back a raw address so that the pool tests can drive the binding
  // entry point directly. A registry keyed on that address is only sound if
  // the address is the same on a later call, so that is worth pinning here
  // rather than discovering later.
  uintptr_t add_first = get_adder(0);
  uintptr_t multiply = get_adder(1);
  uintptr_t add_again = get_adder(0);

  ASSERT_NE(add_first, 0u);
  ASSERT_NE(multiply, 0u);
  EXPECT_EQ(add_first, add_again);
  EXPECT_NE(add_first, multiply);

  // An index nobody implements stays null rather than becoming some non-null
  // address.
  EXPECT_EQ(get_adder(99), 0u);
}

// A callback handed back out of the sandbox. Unlike everything above, these
// go through the library's own signature, so they read the same and mean the
// same whether or not there is a sandbox underneath.

TEST(ReturnedCallback, CanBeCalled) {
  int (*add)(int, int) = get_adder_fn(0);
  ASSERT_NE(add, nullptr);
  EXPECT_EQ(add(2, 3), 5);

  int (*multiply)(int, int) = get_adder_fn(1);
  ASSERT_NE(multiply, nullptr);
  EXPECT_EQ(multiply(2, 3), 6);
}

TEST(ReturnedCallback, NullStaysNull) {
  // A library's `if (cb)` has to keep working across the boundary.
  EXPECT_EQ(get_adder_fn(99), nullptr);
}

TEST(ReturnedCallback, CanBePassedBackIn) {
  // Real-world pattern (e.g. `lua_gethook` / `lua_sethook`, or `sqlite3_vfs`
  // methods forwarded by a wrapper VFS): a callback implemented by the
  // sandboxee is handed out to the host and then passed back into the library
  // as a callback argument. Under the sandbox, the host thunk unwraps back to
  // the original sandboxee function pointer when crossing in.
  EXPECT_EQ(apply_twice(get_adder_fn(0), 2, 3), 8);
  EXPECT_EQ(apply_twice(get_adder_fn(1), 2, 3), 18);
}

TEST(ReturnedCallback, IsInterchangeableWithAHostFunction) {
  // Same call, same callback typedef, once with each kind of callback.
  auto host_add = [](int a, int b) { return a + b; };
  EXPECT_EQ(apply_twice(host_add, 2, 3), 8);
  EXPECT_EQ(apply_twice(get_adder_fn(0), 2, 3), 8);
}

TEST(ReturnedCallback, AskingTwiceGivesTheSameCallback) {
  // Equality of function pointers is what a library's "is this still my
  // hook?" check rests on, so the substitution has to preserve it.
  EXPECT_EQ(get_adder_fn(0), get_adder_fn(0));
  EXPECT_NE(get_adder_fn(0), get_adder_fn(1));
}

// A callback the library keeps past the call that handed it over (e.g.
// `libxml2`'s `xmlRegisterInputCallbacks` in `src/xmlIO.c`, `lua`'s
// `lua_sethook` in `monitoring/platform/automon/lua/sandbox/enforcer.cc`, or
// `cairo`'s `cairo_user_font_face_set_init_func`). Because the callback is
// registered in `HostCallbackRegistry`, its trampoline slot stays alive after
// the installing call returns so later library calls can still invoke it.

TEST(RetainedCallback, SurvivesTheCallThatInstalledIt) {
  static int calls = 0;
  calls = 0;
  install_hook([](int a, int b) {
    ++calls;
    return a + b;
  });

  EXPECT_EQ(run_hook(2, 3), 5);
  EXPECT_EQ(calls, 1);
}

TEST(RetainedCallback, StaysUsableAcrossSeveralCalls) {
  install_hook([](int a, int b) { return a * b; });

  EXPECT_EQ(run_hook(2, 3), 6);
  EXPECT_EQ(run_hook(4, 5), 20);
}

TEST(RetainedCallback, RoundTripsBackToTheSamePointer) {
  // Real-world pattern: `lua_sethook` (`enforcer.cc`) followed by `lua_gethook`
  // (`reader.cc` / `ldblib.c`), where the active hook may have been installed
  // either by the host or by the sandboxee itself.
  // When the host installs one of its own functions and asks for it back,
  // `LookUpTrampoline` unwraps the trampoline back to the original host
  // function pointer rather than wrapping it in a host thunk.
  int (*host_add)(int, int) = [](int a, int b) { return a + b; };
  install_hook(host_add);
  EXPECT_EQ(get_hook(), host_add);

  // And when the host installs a callback the sandboxee originally handed out,
  // `LookUpThunk` unwraps it going in and `ThunkFor` hands back the same thunk
  // coming out.
  int (*sandbox_mul)(int, int) = get_adder_fn(1);
  install_hook(sandbox_mul);
  EXPECT_EQ(get_hook(), sandbox_mul);
  EXPECT_EQ(run_hook(3, 4), 12);

  install_hook(nullptr);
  EXPECT_EQ(get_hook(), nullptr);
}

TEST(StructCallbackField, HostCallbackInStructIsCalledByTheLibrary) {
  // Real-world pattern: `sox_create_effect` (`audio/util/sox_helper.cc`) or
  // `libxml2` DOM callbacks
  // (`net/proto2/contrib/xml/internal/xml2dom_legacy.cc`), where a struct is
  // aggregate-initialized on the host with host callbacks and passed into the
  // sandboxed library.
  CalculatorHooks hooks{
      .combine = [](int a, int b) { return a + b; },
      .initial_value = 10,
  };
  EXPECT_EQ(run_with_hooks(&hooks, 2, 3), 15);

  CalculatorHooks null_hooks{
      .combine = nullptr,
      .initial_value = 42,
  };
  EXPECT_EQ(run_with_hooks(&null_hooks, 2, 3), 42);
}

TEST(StructCallbackField, SandboxPopulatedFieldCanBeCalledByHostAndPassedBack) {
  // Real-world pattern (`libwebp`'s `WebPPictureInit` + `WebPEncode` in
  // `image/codec/webpencoder.cc`, `libxml2`'s `xmlSAX2InitDefaultSAXHandler` in
  // `bigtable/contrib/btv/btv_xml.cc`, and `sqlite3_vfs_find` /
  // `sqlite3_vfs_register` in `ads/branding/grp/utils/in_memory_sql/db.cc`):
  // the library initializes a struct with its own default callbacks and hands
  // it back to the host, which may call the field directly, pass the struct
  // back unmodified, or override the field with a host callback.
  CalculatorHooks hooks{};
  init_default_hooks(&hooks, 0);
  ASSERT_NE(hooks.combine, nullptr);
  EXPECT_EQ(hooks.initial_value, 10);

  // Host calls the sandboxee-populated struct field directly.
  EXPECT_EQ(hooks.combine(4, 5), 9);

  // Host passes the struct back into the library unmodified; the library calls
  // its own default callback (`(10 + 2) + 3 = 15`).
  EXPECT_EQ(run_with_hooks(&hooks, 2, 3), 15);

  // Host overrides the callback field with a host function and passes the
  // struct back into the library (`(10 + 2) - 3 = 9`).
  hooks.combine = [](int a, int b) { return a - b; };
  EXPECT_EQ(run_with_hooks(&hooks, 2, 3), 9);

  // Null from the sandbox stays null on the host.
  init_default_hooks(&hooks, 99);
  EXPECT_EQ(hooks.combine, nullptr);
}

TEST(StructCallbackField,
     InOutStructPreservesHostPointerAndRetainsRegistrationAcrossCalls) {
  int (*host_sub)(int, int) = [](int a, int b) { return a - b; };
  CalculatorHooks hooks{
      .combine = host_sub,
      .initial_value = 10,
  };

  install_struct_hooks(&hooks);
  // The library incremented `initial_value` in place; the post-call sync
  // copied the updated struct back and unwrapped `hooks.combine` back to
  // `host_sub` rather than leaving the sandboxee trampoline address in the
  // host's struct.
  EXPECT_EQ(hooks.initial_value, 11);
  EXPECT_EQ(hooks.combine, host_sub);
  EXPECT_EQ(hooks.combine(9, 4), 5);

  // And the callback registration stored in `g_struct_hooks` inside the
  // sandbox survives past `install_struct_hooks` returning: `(10 + 2) - 3 = 9`.
  EXPECT_EQ(run_installed_struct_hooks(2, 3), 9);
}

TEST(StructCallbackField, MarshalsBufferParametersInBothDirections) {
  const uint8_t kData[] = {10, 20, 30, 40};

  // Forward direction: host provides a byte-processing callback in a struct;
  // the generated registration wrapper marshals `buf` out of the sandbox.
  BufferProcessor host_proc{
      .process_bytes =
          [](const uint8_t* buf, size_t len) {
            int acc = 0;
            for (size_t i = 0; i < len; ++i) {
              acc += static_cast<int>(buf[i]) * static_cast<int>(i + 1);
            }
            return acc;
          },
      .scale = 3,
  };
  // (10*1 + 20*2 + 30*3 + 40*4) * 3 = 300 * 3 = 900.
  EXPECT_EQ(run_buffer_processor(&host_proc, kData, sizeof(kData)), 900);

  // Reverse direction: sandbox populates `proc.process_bytes` with its own
  // `SumBytes` function; host can both call `proc.process_bytes` directly on a
  // host buffer and pass `&proc` back into `run_buffer_processor`.
  BufferProcessor sb_proc{};
  init_default_buffer_processor(&sb_proc);
  ASSERT_NE(sb_proc.process_bytes, nullptr);
  EXPECT_EQ(sb_proc.scale, 2);
  EXPECT_EQ(sb_proc.process_bytes(kData, sizeof(kData)), 100);
  EXPECT_EQ(run_buffer_processor(&sb_proc, kData, sizeof(kData)), 200);
}

static int g_host_processed_acc = 0;

void HostProcessBuffer(const char* buf, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    g_host_processed_acc += static_cast<unsigned char>(buf[i]) * 10;
  }
}

void HostUnusedCallback(const char* cstr) {
  for (const char* p = cstr; *p != '\0'; ++p) {
    g_host_processed_acc += static_cast<unsigned char>(*p);
  }
}

// Exercises the full caller-callee cross-product (`{sandbox, host} ->
// {sandbox, host}`) modeled on `libjpeg_turbo`'s `jpeg_error_mgr` /
// `jpeg_CreateDecompress` (`image/codec/jpegdecoder.cc` and `src/jerror.c`),
// `libxml2` SAX2 (`bigtable/contrib/btv/btv_xml.cc`), and `lua` hook
// registration (`monitoring/platform/automon/lua/sandbox/`):
//   1. `init_jpeg_style_callbacks(&s)` populates multiple callback fields
//      (`SANDBOX_BYTE_SIZED_BY(size)` and `SANDBOX_NULL_TERMINATED`) with
//      private sandboxee default functions.
//   2. The host either keeps `s.process_buffer` as the sandboxee default
//      (Branch 1) or overwrites it with `&HostProcessBuffer` (Branch 2).
//   3. `call_process_buffs(&s)` invokes `s.process_buffer` from inside the
//      sandbox (`sb -> sb` in Branch 1, `sb -> host` in Branch 2).
//   4. `s.process_buffer(...)` and `s.unused_callback(...)` invoke the struct
//      fields directly from the host (`host -> sb` or `host -> host`).
//   5. `call_process_buffer_callback(s.process_buffer, ...)` passes the struct
//      field as a function-parameter callback (`sb -> {host, sb}`).
//   6. `register_process_buffer_callback` /
//   `get_registered_process_buffer_callback`
//      retain and return `{host, sb}` pointers across calls while preserving
//      pointer identity (`host_or_sb == s.process_buffer`).
TEST(StructCallbackField,
     MultiFieldHandlerTableSupportsFullCallerCalleeCrossProduct) {
  const char kFourBytes[] = "\x05\x06\x07\x08";  // sum = 26

  // Branch 1: `s.process_buffer` stays the sandboxee default
  // (`PrivDefaultProcessBuffer`).
  {
    reset_sandbox_processed_acc();
    g_host_processed_acc = 0;

    JpegStyleCallbacks s{};
    init_jpeg_style_callbacks(&s);
    ASSERT_NE(s.process_buffer, nullptr);
    ASSERT_NE(s.unused_callback, nullptr);

    // (a) sb -> sb: `call_process_buffs` feeds {1,2,3,4} (sum = 10).
    call_process_buffs(&s);
    EXPECT_EQ(get_sandbox_processed_acc(), 10);

    // (b) host -> sb: host invokes both `SANDBOX_BYTE_SIZED_BY` and
    // `SANDBOX_NULL_TERMINATED` callbacks directly.
    s.process_buffer(kFourBytes, 4);
    EXPECT_EQ(get_sandbox_processed_acc(), 10 + 26);
    s.unused_callback("AB");  // 'A'(65) + 'B'(66) = 131
    EXPECT_EQ(get_sandbox_processed_acc(), 36 + 131);

    // Function parameter callback receiving the struct's sandboxee callback:
    call_process_buffer_callback(s.process_buffer, kFourBytes, 4);
    EXPECT_EQ(get_sandbox_processed_acc(), 167 + 26);

    // Register and retrieve back the sandboxee callback: pointer identity
    // holds.
    register_process_buffer_callback(1, s.process_buffer);
    auto host_or_sb = get_registered_process_buffer_callback(1);
    EXPECT_EQ(host_or_sb, s.process_buffer);
    host_or_sb(kFourBytes, 4);
    EXPECT_EQ(get_sandbox_processed_acc(), 193 + 26);
    EXPECT_EQ(g_host_processed_acc, 0);
  }

  // Branch 2: Host overwrites `s.process_buffer = &HostProcessBuffer`.
  {
    reset_sandbox_processed_acc();
    g_host_processed_acc = 0;

    JpegStyleCallbacks s{};
    init_jpeg_style_callbacks(&s);
    s.process_buffer = &HostProcessBuffer;

    // (a) sb -> host: `call_process_buffs` feeds {1,2,3,4} -> 10 * 10 = 100.
    call_process_buffs(&s);
    EXPECT_EQ(g_host_processed_acc, 100);
    EXPECT_EQ(get_sandbox_processed_acc(), 0);

    // (b) host -> host: direct call to `s.process_buffer` (26 * 10 = 260).
    s.process_buffer(kFourBytes, 4);
    EXPECT_EQ(g_host_processed_acc, 360);

    // Function parameter callback receiving the host-overwritten field:
    call_process_buffer_callback(s.process_buffer, kFourBytes, 4);
    EXPECT_EQ(g_host_processed_acc, 620);

    // Register and retrieve back the host callback: unwraps to
    // `&HostProcessBuffer`.
    register_process_buffer_callback(1, s.process_buffer);
    auto host_or_sb = get_registered_process_buffer_callback(1);
    EXPECT_EQ(host_or_sb, &HostProcessBuffer);
    host_or_sb(kFourBytes, 4);
    EXPECT_EQ(g_host_processed_acc, 880);
  }
}

TEST(RetainedCallback, MultithreadedInstallReceiveAndInvokeAcrossThreads) {
  auto host_add = +[](int a, int b) { return a + b; };
  int (*sb_mul)(int, int) = nullptr;

  std::thread installer([&] {
    install_hook(host_add);
    sb_mul = get_adder_fn(1);
  });
  installer.join();
  ASSERT_NE(sb_mul, nullptr);

  int hook_res = 0;
  int sb_res = 0;
  int (*unwrapped_hook)(int, int) = nullptr;
  std::thread caller([&] {
    hook_res = run_hook(19, 23);
    sb_res = sb_mul(6, 7);
    unwrapped_hook = get_hook();
  });
  caller.join();

  EXPECT_EQ(hook_res, 42);
  EXPECT_EQ(sb_res, 42);
  EXPECT_EQ(unwrapped_hook, host_add);
  install_hook(nullptr);
}

TEST(StructCallbackField, NestedStructCallbackFieldsRoundTrip) {
  reset_sandbox_processed_acc();
  g_host_processed_acc = 0;

  NestedJpegPipeline pipeline{};
  init_nested_jpeg_pipeline(&pipeline, 77);
  EXPECT_EQ(pipeline.pipeline_id, 77);
  ASSERT_NE(pipeline.callbacks.process_buffer, nullptr);
  ASSERT_NE(pipeline.callbacks.unused_callback, nullptr);

  // Run with both nested callbacks pointing at sandboxee defaults:
  // `call_process_buffs` adds 10, `unused_callback("OK")` adds
  // 'O'(79)+'K'(75)=154.
  EXPECT_EQ(run_nested_jpeg_pipeline(&pipeline), 77);
  EXPECT_EQ(get_sandbox_processed_acc(), 164);

  // Override only the nested `unused_callback` (`SANDBOX_NULL_TERMINATED`) with
  // a host function while keeping `process_buffer` as the sandboxee default:
  pipeline.callbacks.unused_callback = &HostUnusedCallback;
  EXPECT_EQ(run_nested_jpeg_pipeline(&pipeline), 77);
  EXPECT_EQ(get_sandbox_processed_acc(), 164 + 10);
  EXPECT_EQ(g_host_processed_acc, 154);

  // `cstr_cb` is a single-shot callback typedef (`idempotent == false`), so
  // passing `&pipeline` into a second RPC re-arms `HostUnusedCallback` for one
  // more invocation.
  EXPECT_EQ(run_nested_jpeg_pipeline(&pipeline), 77);
  EXPECT_EQ(get_sandbox_processed_acc(), 174 + 10);
  EXPECT_EQ(g_host_processed_acc, 154 + 154);
}

bool IsReverseCbSandboxed() {
  int probe = 42;
  return is_reverse_cb_marshaled_copy(&probe,
                                      reinterpret_cast<uintptr_t>(&probe)) != 0;
}

TEST(SingleShotCallback, RejectsSecondInvocationWithoutIdempotentAnnotation) {
  auto doubler = +[](int x) { return x * 2; };
  EXPECT_EQ(invoke_single_shot_callback_n_times(1, doubler), 2);
  EXPECT_EQ(invoke_single_shot_callback_n_times(0, doubler), 0);

  if (IsReverseCbSandboxed()) {
    EXPECT_DEATH(invoke_single_shot_callback_n_times(2, doubler), "");
  } else {
    EXPECT_EQ(invoke_single_shot_callback_n_times(2, doubler), 6);
  }
}

TEST(ReverseCallback, CrossTypedefTrampolineSubstitutionFailsClosed) {
  auto inc = +[](int x) { return x + 1; };
  EXPECT_EQ(swap_callback_typedef_trampoline(nullptr), nullptr);

  if (IsReverseCbSandboxed()) {
    EXPECT_DEATH(swap_callback_typedef_trampoline(inc),
                 "registered for callback typedef");
  } else {
    unary_b_cb returned = swap_callback_typedef_trampoline(inc);
    ASSERT_NE(returned, nullptr);
    EXPECT_EQ(returned(41), 42);
  }
}

// Exercises non-`char` null-terminated scalar and enum sequences across
// function arguments, forward callbacks, and reverse callbacks:
// - `libxml2`: `const xmlChar*` (`const unsigned char*`) in `xmlStrlen` /
//   `startElementSAXFunc`.
// - SQLite: `const uint16_t*` UTF-16 null-terminated strings in
//   `sqlite3_open16` / `sqlite3_complete16`.
// - Zero-terminated enum opcode arrays (`kXmlOpEnd == 0`).
TEST(NullTerminatedNonCharArrays,
     SupportsUnsignedCharUtf16AndEnumNullTerminatedArrays) {
  const xml_char_t kRootTag[] = {'r', 'o', 'o', 't', 0};  // 114+111+111+116=452
  EXPECT_EQ(xml_utf8_byte_sum(kRootTag), 452);
  EXPECT_EQ(xml_utf8_byte_sum(nullptr), -1);

  struct SaxState {
    int count = 0;
    int byte_sum = 0;
  };
  SaxState state{};

  // Forward callback receiving `const xml_char_t*` (`const unsigned char*`)
  // from both a caller-supplied tag (`"root"`, sum 452) and a sandbox-internal
  // tag (`"item"`, 105+116+101+109 = 431).
  dispatch_xml_sax_element(
      &state,
      [](void* ctx, const xml_char_t* name) {
        auto* s = static_cast<SaxState*>(ctx);
        s->count += 1;
        if (name != nullptr) {
          for (const xml_char_t* p = name; *p != 0; ++p) {
            s->byte_sum += static_cast<int>(*p);
          }
        }
      },
      kRootTag);
  EXPECT_EQ(state.count, 2);
  EXPECT_EQ(state.byte_sum, 452 + 431);

  // Reverse callback: host invokes a sandboxee-implemented callback taking
  // `const xml_char_t*` (`SANDBOX_NULL_TERMINATED`).
  reset_sandbox_processed_acc();
  auto sb_xml_cb = get_default_xml_element_cb();
  ASSERT_NE(sb_xml_cb, nullptr);
  sb_xml_cb(nullptr, kRootTag);
  EXPECT_EQ(get_sandbox_processed_acc(), 452);

  // SQLite-style UTF-16 (`const uint16_t*`) null-terminated array in both
  // function parameter and callback parameter positions.
  const uint16_t kSql16[] = {'S', 'E', 'L', 0};  // 83 + 69 + 76 = 228
  int total = utf16_code_unit_sum(kSql16, [](const uint16_t* segment) -> int {
    int s = 0;
    for (const uint16_t* p = segment; *p != 0; ++p) {
      s += static_cast<int>(*p);
    }
    return s;
  });
  // 228 + (0x003B + 0x03A9 = 59 + 937 = 996) = 1224
  EXPECT_EQ(total, 228 + 59 + 937);
  EXPECT_EQ(utf16_code_unit_sum(nullptr, nullptr), -1);

  // `INOUT` struct with an `inout_ptr` `SANDBOX_NULL_TERMINATED` UTF-16 member:
  // verifies in-place mutation, re-zeroing a clobbered trailing sentinel on
  // return to the host, and safe handling when the sandboxee overwrites the
  // struct's pointer field with `nullptr`.
  uint16_t mutable_utf16[] = {'A', 'B', 0};
  Utf16MutablePacket pkt{.text16 = mutable_utf16, .mode = 0};
  mutate_or_clear_utf16_packet(&pkt);
  ASSERT_EQ(pkt.text16, mutable_utf16);
  EXPECT_EQ(mutable_utf16[0], 'B');
  EXPECT_EQ(mutable_utf16[1], 'C');
  EXPECT_EQ(mutable_utf16[2], 0);

  if (IsReverseCbSandboxed()) {
    pkt.mode = 2;
    mutate_or_clear_utf16_packet(&pkt);
    ASSERT_EQ(pkt.text16, mutable_utf16);
    EXPECT_EQ(mutable_utf16[0], 'Z');
    EXPECT_EQ(mutable_utf16[1], 'Z');
    EXPECT_EQ(mutable_utf16[2], 0);
  }

  pkt.mode = 1;
  mutate_or_clear_utf16_packet(&pkt);
  EXPECT_EQ(pkt.text16, nullptr);
  EXPECT_EQ(mutable_utf16[2], 0);

  // Context-bound multi-byte UTF-16 (`const uint16_t*`) and enum
  // (`const XmlFilterOp*`) null-terminated getters (`sqlite3_errmsg16` style),
  // including empty `{0}` sentinels.
  XmlFilterCtx* ctx = create_xml_filter_ctx(/*empty=*/0);
  ASSERT_NE(ctx, nullptr);
  const XmlFilterOp* ctx_ops = get_xml_filter_ctx_ops(ctx);
  ASSERT_NE(ctx_ops, nullptr);
  EXPECT_EQ(ctx_ops[0], kXmlOpAddTen);
  EXPECT_EQ(ctx_ops[1], kXmlOpDouble);
  EXPECT_EQ(ctx_ops[2], kXmlOpAddLen);
  EXPECT_EQ(ctx_ops[3], kXmlOpEnd);
  const uint16_t* errmsg16 = get_xml_filter_ctx_errmsg16(ctx);
  ASSERT_NE(errmsg16, nullptr);
  EXPECT_EQ(errmsg16[0], 'E');
  EXPECT_EQ(errmsg16[1], 0x03A9);
  EXPECT_EQ(errmsg16[2], '!');
  EXPECT_EQ(errmsg16[3], 0);
  destroy_xml_filter_ctx(ctx);

  XmlFilterCtx* empty_ctx = create_xml_filter_ctx(/*empty=*/1);
  ASSERT_NE(empty_ctx, nullptr);
  const XmlFilterOp* empty_ops = get_xml_filter_ctx_ops(empty_ctx);
  ASSERT_NE(empty_ops, nullptr);
  EXPECT_EQ(empty_ops[0], kXmlOpEnd);
  const uint16_t* empty_msg16 = get_xml_filter_ctx_errmsg16(empty_ctx);
  ASSERT_NE(empty_msg16, nullptr);
  EXPECT_EQ(empty_msg16[0], 0);
  destroy_xml_filter_ctx(empty_ctx);

  // Zero-terminated enum sequence (`kXmlOpEnd == 0`).
  const XmlFilterOp kOps[] = {kXmlOpAddLen, kXmlOpAddTen, kXmlOpDouble,
                              kXmlOpAddLen, kXmlOpEnd};
  // len("root") == 4: (((0 + 4) + 10) * 2) + 4 = 32
  EXPECT_EQ(run_xml_filter_ops(kOps, kRootTag), 32);
  EXPECT_EQ(run_xml_filter_ops(nullptr, kRootTag), -1);
}
}  // namespace
