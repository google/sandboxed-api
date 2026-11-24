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

#include "sandboxed_api/lwbox/runtime/lwbox_runtime.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <thread>  // NOLINT(build/c++11)
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/sandbox.h"
#include "sandboxed_api/var_callback.h"
#include "sandboxed_api/var_ptr.h"
#include "sandboxed_api/var_type.h"

// `ContextBindingRegistry` annotates its pointer parameters `absl_nonnull` to
// document the contract, but still `CHECK`-fails on null so that a violation
// crashes diagnosably instead of corrupting state. Exercising those `CHECK`s
// requires deliberately passing null, which the nullability sanitizer traps at
// the call site before the callee ever runs. Tests that do so must opt out.
// Clang-only: GCC has no nullability sanitizer.
#if defined(__clang__)
#define NO_SANITIZE_NULLABILITY_ATTR __attribute__((no_sanitize("nullability")))
#else
#define NO_SANITIZE_NULLABILITY_ATTR
#endif

namespace sapi {
namespace lwbox {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::_;
using ::testing::HasSubstr;
using ::testing::Return;

class MockRPCChannel : public sapi::RPCChannel {
 public:
  MOCK_METHOD(absl::Status, Call,
              (const FuncCall&, uint32_t, FuncRet*, v::Type), (override));
  MOCK_METHOD(absl::Status, Allocate, (size_t, void**, bool), (override));
  MOCK_METHOD(absl::Status, Reallocate, (void*, size_t, void**), (override));
  MOCK_METHOD(absl::Status, Free, (void*), (override));
  MOCK_METHOD(absl::StatusOr<size_t>, CopyFromSandbox,
              (uintptr_t, absl::Span<char>), (override));
  MOCK_METHOD(absl::StatusOr<size_t>, CopyToSandbox,
              (uintptr_t, absl::Span<const char>), (override));
  MOCK_METHOD(absl::Status, Symbol, (const char*, void**), (override));
  MOCK_METHOD(absl::Status, Exit, (), (override));
  MOCK_METHOD(absl::Status, SendFD, (int, int*), (override));
  MOCK_METHOD(absl::Status, RecvFD, (int, int*), (override));
  MOCK_METHOD(absl::Status, Close, (int), (override));
  MOCK_METHOD(absl::StatusOr<std::unique_ptr<RPCChannel>>, SpawnThread, (),
              (override));
  MOCK_METHOD(absl::StatusOr<size_t>, Strlen, (void*), (override));
  MOCK_METHOD(absl::StatusOr<uintptr_t>, RegisterCallback,
              (absl::AnyInvocable<uint64_t(absl::Span<const uint64_t>)>),
              (override));
  MOCK_METHOD(absl::Status, UnregisterCallback, (uintptr_t), (override));
};

class MockSandbox : public sapi::SandboxBase {
 public:
  explicit MockSandbox(sapi::RPCChannel* channel) : channel_(channel) {}

  void set_channel(sapi::RPCChannel* channel) { channel_ = channel; }
  absl::Status Init() override { return absl::OkStatus(); }
  bool is_active() const override { return true; }
  void Terminate(bool attempt_graceful_exit = true) override {}
  sapi::RPCChannel* rpc_channel() const override { return channel_; }
  absl::Status AwaitExitStatus() override { return absl::OkStatus(); }
  absl::Status SetWallTimeLimit(absl::Duration limit) const override {
    return absl::OkStatus();
  }
  absl::StatusOr<int> GetPid() const override { return 1234; }

 private:
  sapi::RPCChannel* channel_;
};

// =========================================================================
// ContextBindingRegistry & SyncFromSandboxToHost Tests
// =========================================================================

TEST(LwboxRuntimeTest, ContextBindingRegistryBasic) {
  auto* reg = ContextBindingRegistry::Instance();
  int dummy_context = 42;

  EXPECT_EQ(reg->BindSize(&dummy_context, "len", 100), std::nullopt);
  EXPECT_EQ(reg->GetSize(&dummy_context, "len"), 100);

  EXPECT_EQ(reg->BindSize(&dummy_context, "len", 200), 100);
  EXPECT_EQ(reg->GetSize(&dummy_context, "len"), 200);

  MockRPCChannel mock_channel;
  MockSandbox sandbox(&mock_channel);
  reg->ClearBindings(sandbox, &dummy_context);

  EXPECT_EQ(reg->BindSize(&dummy_context, "len", 300), std::nullopt);
  EXPECT_EQ(reg->GetSize(&dummy_context, "len"), 300);

  reg->ClearBindings(sandbox, &dummy_context);
}

TEST(LwboxRuntimeTest, ContextBindingRegistryMultiContextIsolation) {
  auto* reg = ContextBindingRegistry::Instance();
  int ctx1 = 1;
  int ctx2 = 2;

  reg->BindSize(&ctx1, "size", 10);
  reg->BindSize(&ctx2, "size", 20);

  EXPECT_EQ(reg->GetSize(&ctx1, "size"), 10);
  EXPECT_EQ(reg->GetSize(&ctx2, "size"), 20);

  MockRPCChannel mock_channel;
  MockSandbox sandbox(&mock_channel);
  void* fake_remote1 = reinterpret_cast<void*>(0x4000);
  void* fake_remote2 = reinterpret_cast<void*>(0x5000);

  reg->RetainPointer(&ctx1, "buf", 0x4000, 10);
  reg->RetainPointer(&ctx2, "buf", 0x5000, 20);

  EXPECT_CALL(mock_channel, Free(fake_remote1))
      .WillOnce(Return(absl::OkStatus()));
  reg->ClearBindings(sandbox, &ctx1);

  // ctx2 retained pointer should still be intact and can be cleared separately
  EXPECT_CALL(mock_channel, Free(fake_remote2))
      .WillOnce(Return(absl::OkStatus()));
  reg->ClearBindings(sandbox, &ctx2);
}

TEST(LwboxRuntimeTest, ContextBindingRegistryNullHandling) {
  auto* reg = ContextBindingRegistry::Instance();
  MockRPCChannel mock_channel;
  MockSandbox sandbox(&mock_channel);

  // ClearBindings safely no-ops on null context
  reg->ClearBindings(sandbox, nullptr);

  int ctx = 3;
  // Retaining remote_ptr == 0 safely no-ops
  reg->RetainPointer(&ctx, "empty", 0, 0);
  reg->ClearBindings(sandbox, &ctx);
}

TEST(LwboxRuntimeTest, ContextBindingRegistryHostBufferAndSync) {
  auto* reg = ContextBindingRegistry::Instance();
  int dummy_context = 99;

  MockRPCChannel mock_channel;
  MockSandbox sandbox(&mock_channel);

  EXPECT_EQ(reg->GetBoundHostBuffer(&dummy_context, "buf"), std::nullopt);

  void* host_buf = std::malloc(64);
  ASSERT_NE(host_buf, nullptr);
  std::memset(host_buf, 0xAA, 64);

  reg->BindHostBuffer(&dummy_context, "buf", 0x9000, host_buf, 64);

  auto bound = reg->GetBoundHostBuffer(&dummy_context, "buf");
  ASSERT_TRUE(bound.has_value());
  EXPECT_EQ(bound->remote_ptr, 0x9000);
  EXPECT_EQ(bound->host_ptr, host_buf);
  EXPECT_EQ(bound->bytes, 64);

  // SyncFromSandboxToHost copy
  EXPECT_CALL(mock_channel, CopyFromSandbox(0x9000, _)).WillOnce(Return(64));
  SyncFromSandboxToHost(sandbox, bound->remote_ptr, bound->host_ptr,
                        bound->bytes);

  // ClearBindings must free the host buffer via std::free
  reg->ClearBindings(sandbox, &dummy_context);
  EXPECT_EQ(reg->GetBoundHostBuffer(&dummy_context, "buf"), std::nullopt);
}

TEST(LwboxRuntimeTest,
     ContextBindingRegistryHostBufferReplacementFreesPrevious) {
  auto* reg = ContextBindingRegistry::Instance();
  int dummy_context = 101;

  MockRPCChannel mock_channel;
  MockSandbox sandbox(&mock_channel);

  void* host_buf1 = std::malloc(32);
  ASSERT_NE(host_buf1, nullptr);
  reg->BindHostBuffer(&dummy_context, "buf", 0x9100, host_buf1, 32);

  void* host_buf2 = std::malloc(64);
  ASSERT_NE(host_buf2, nullptr);
  // Replacing binding frees host_buf1 and registers host_buf2
  reg->BindHostBuffer(&dummy_context, "buf", 0x9200, host_buf2, 64);

  auto bound = reg->GetBoundHostBuffer(&dummy_context, "buf");
  ASSERT_TRUE(bound.has_value());
  EXPECT_EQ(bound->remote_ptr, 0x9200);
  EXPECT_EQ(bound->host_ptr, host_buf2);
  EXPECT_EQ(bound->bytes, 64);

  reg->ClearBindings(sandbox, &dummy_context);
  EXPECT_EQ(reg->GetBoundHostBuffer(&dummy_context, "buf"), std::nullopt);
}

TEST(LwboxRuntimeTest, ContextBindingRegistryCheckFailures)
NO_SANITIZE_NULLABILITY_ATTR {
  auto* reg = ContextBindingRegistry::Instance();
  int dummy_context = 102;
  MockRPCChannel mock_channel;
  MockSandbox sandbox(&mock_channel);

  // GetSize for missing label must CHECK-fail
  EXPECT_DEATH(reg->GetSize(&dummy_context, "missing_label"),
               "Missing context size binding for label: missing_label");

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnonnull"
#endif
  // BindSize with null context must CHECK-fail
  EXPECT_DEATH(reg->BindSize(nullptr, "len", 10),
               "BindSize called with null context");

  // GetSize with null context must CHECK-fail
  EXPECT_DEATH(reg->GetSize(nullptr, "len"),
               "GetSize called with null context");

  // RetainPointer with null context must CHECK-fail
  EXPECT_DEATH(reg->RetainPointer(nullptr, "buf", 0x1000, 10),
               "RetainPointer called with null context");

  // GetBoundHostBuffer with null context must CHECK-fail
  EXPECT_DEATH(reg->GetBoundHostBuffer(nullptr, "buf"),
               "GetBoundHostBuffer called with null context");

  // BindHostBuffer with null context must CHECK-fail
  void* host_buf = std::malloc(16);
  EXPECT_DEATH(reg->BindHostBuffer(nullptr, "buf", 0x1000, host_buf, 16),
               "BindHostBuffer called with null context");

  // BindHostBuffer with null host_ptr must CHECK-fail
  EXPECT_DEATH(reg->BindHostBuffer(&dummy_context, "buf", 0x1000, nullptr, 16),
               "BindHostBuffer called with null host_ptr");
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  // Conflicting RetainPointer without prior ClearBindings must CHECK-fail
  reg->RetainPointer(&dummy_context, "conflict_buf", 0x2000, 20);
  EXPECT_CALL(mock_channel, Free(reinterpret_cast<void*>(0x2000)))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_DEATH(reg->RetainPointer(&dummy_context, "conflict_buf", 0x3000, 30),
               "RetainPointer overwriting previously retained pointer");

  reg->ClearBindings(sandbox, &dummy_context);
  std::free(host_buf);
}

TEST(LwboxRuntimeTest, ClearBindingsFailsWhenRpcChannelIsNullWithRetained) {
  auto* reg = ContextBindingRegistry::Instance();
  int dummy_context = 43;

  reg->RetainPointer(&dummy_context, "buf", 0x4000, 10);

  MockSandbox sandbox_no_channel(nullptr);
  EXPECT_DEATH(reg->ClearBindings(sandbox_no_channel, &dummy_context),
               "Sandbox RPCChannel is null while clearing remote bindings");

  MockRPCChannel mock_channel;
  MockSandbox sandbox(&mock_channel);
  EXPECT_CALL(mock_channel, Free(reinterpret_cast<void*>(0x4000)))
      .WillOnce(Return(absl::OkStatus()));
  reg->ClearBindings(sandbox, &dummy_context);
}

TEST(LwboxRuntimeTest, ClearBindingsPropagatesFreeError) {
  auto* reg = ContextBindingRegistry::Instance();
  int dummy_context = 44;
  void* fake_remote = reinterpret_cast<void*>(0x8100);

  reg->RetainPointer(&dummy_context, "buf",
                     reinterpret_cast<uintptr_t>(fake_remote), 1024);

  EXPECT_DEATH(
      {
        MockRPCChannel mock_channel;
        MockSandbox sandbox(&mock_channel);
        EXPECT_CALL(mock_channel, Free(fake_remote))
            .WillOnce(Return(absl::InternalError("Free error")));
        reg->ClearBindings(sandbox, &dummy_context);
      },
      "Failed to free retained remote pointer");

  MockRPCChannel mock_channel;
  MockSandbox sandbox(&mock_channel);
  EXPECT_CALL(mock_channel, Free(fake_remote))
      .WillOnce(Return(absl::OkStatus()));
  reg->ClearBindings(sandbox, &dummy_context);
}

TEST(LwboxRuntimeTest, ClearBindingsDeduplicatesPointers) {
  auto* reg = ContextBindingRegistry::Instance();
  int dummy_context = 45;

  reg->RetainPointer(&dummy_context, "buf1", 0x4000, 10);
  // Re-retaining same pointer under different label should not double-free
  reg->RetainPointer(&dummy_context, "buf2", 0x4000, 10);

  MockRPCChannel mock_channel;
  MockSandbox sandbox(&mock_channel);
  EXPECT_CALL(mock_channel, Free(reinterpret_cast<void*>(0x4000)))
      .Times(1)
      .WillOnce(Return(absl::OkStatus()));

  reg->ClearBindings(sandbox, &dummy_context);
}

TEST(LwboxRuntimeTest, SyncFromSandboxToHostCheckFailures) {
  MockSandbox sandbox_no_channel(nullptr);
  char buf[8];

  // Null RPC channel must CHECK-fail
  EXPECT_DEATH(
      SyncFromSandboxToHost(sandbox_no_channel, 0x1000, buf, sizeof(buf)),
      "Sandbox RPCChannel is null while syncing buffer from sandbox");

  // Copy failure must CHECK-fail
  EXPECT_DEATH(
      {
        MockRPCChannel mock_channel;
        MockSandbox sandbox(&mock_channel);
        EXPECT_CALL(mock_channel, CopyFromSandbox(0x1000, _))
            .WillOnce(Return(absl::InternalError("Copy error")));
        SyncFromSandboxToHost(sandbox, 0x1000, buf, sizeof(buf));
      },
      "Failed to copy memory from sandbox at 0x1000");
}

// =========================================================================
// GlobalStringRegistry Tests
// =========================================================================

TEST(LwboxRuntimeTest, GlobalStringRegistryGetOrFetch) {
  MockRPCChannel mock_channel;
  MockSandbox sandbox(&mock_channel);

  void* fake_remote = reinterpret_cast<void*>(0xAA00);
  const std::string test_str = "hello global string";
  size_t len = test_str.size();

  EXPECT_CALL(mock_channel, Strlen(fake_remote)).WillOnce(Return(len));
  EXPECT_CALL(mock_channel, CopyFromSandbox(0xAA00, _))
      .WillOnce([&](uintptr_t, absl::Span<char> dst) {
        std::memcpy(dst.data(), test_str.data(), len);
        return len;
      });

  auto* reg = GlobalStringRegistry::Instance();
  const char* result1 = reg->GetOrFetch(sandbox, fake_remote);
  ASSERT_NE(result1, nullptr);
  EXPECT_STREQ(result1, "hello global string");

  // Second call retrieves from cache without invoking sandbox RPC
  EXPECT_CALL(mock_channel, Strlen(_)).Times(0);
  EXPECT_CALL(mock_channel, CopyFromSandbox(_, _)).Times(0);
  const char* result2 = reg->GetOrFetch(sandbox, fake_remote);
  EXPECT_EQ(result1, result2);
}

TEST(LwboxRuntimeTest, GlobalStringRegistryNullHandling) {
  MockRPCChannel mock_channel;
  MockSandbox sandbox(&mock_channel);
  auto* reg = GlobalStringRegistry::Instance();

  EXPECT_EQ(reg->GetOrFetch(sandbox, nullptr), nullptr);
}

TEST(LwboxRuntimeTest, GlobalStringRegistryFailureCheckFails) {
  auto* reg = GlobalStringRegistry::Instance();
  void* fake_remote = reinterpret_cast<void*>(0xBB00);

  EXPECT_DEATH(
      {
        MockRPCChannel mock_channel;
        MockSandbox sandbox(&mock_channel);
        EXPECT_CALL(mock_channel, Strlen(fake_remote))
            .WillOnce(Return(absl::InternalError("Remote strlen failed")));
        reg->GetOrFetch(sandbox, fake_remote);
      },
      "Failed to fetch remote string");
}

// =========================================================================
// SandboxeeCallbackRegistry Tests
// =========================================================================

// The registry never dereferences a thunk address, so these stand in for
// generated thunks: distinct objects, hence distinct addresses.
uintptr_t ThunkAddress(int which) {
  static const int kThunkBodies[8] = {};
  return reinterpret_cast<uintptr_t>(&kThunkBodies[which]);
}

// Two callback typedefs' worth of pools. A pool's address is its typedef's
// identity, so these must be distinct objects that outlive the registry entries
// naming them.
const ThunkPool& PoolA() {
  static const uintptr_t kThunks[] = {ThunkAddress(0), ThunkAddress(1),
                                      ThunkAddress(2)};
  static const ThunkPool kPool = {"byte_sink", absl::MakeConstSpan(kThunks)};
  return kPool;
}

const ThunkPool& PoolB() {
  static const uintptr_t kThunks[] = {ThunkAddress(3), ThunkAddress(4)};
  static const ThunkPool kPool = {"chunk_allocator",
                                  absl::MakeConstSpan(kThunks)};
  return kPool;
}

// The registry is process-wide, so each test hands back what it took. Scoped
// rather than done in a fixture teardown because ForgetSandbox is itself under
// test here.
class ScopedSandbox {
 public:
  ScopedSandbox() : sandbox_(&channel_) {
    ON_CALL(channel_, RegisterCallback(_))
        .WillByDefault(
            [this](absl::AnyInvocable<uint64_t(absl::Span<const uint64_t>)> cb)
                -> absl::StatusOr<uintptr_t> {
              uintptr_t addr = next_trampoline_++;
              last_handler_ = std::move(cb);
              return addr;
            });
    ON_CALL(channel_, UnregisterCallback(_))
        .WillByDefault(Return(absl::OkStatus()));
  }
  ~ScopedSandbox() {
    SandboxeeCallbackRegistry::Instance()->ForgetSandbox(sandbox_);
    HostCallbackRegistry::Instance()->ForgetSandbox(sandbox_);
  }

  MockSandbox& sandbox() { return sandbox_; }
  MockRPCChannel& channel() { return channel_; }
  uint64_t InvokeLastHandler(absl::Span<const uint64_t> args) {
    return last_handler_(args);
  }

 private:
  testing::NiceMock<MockRPCChannel> channel_;
  MockSandbox sandbox_;
  uintptr_t next_trampoline_ = 0x7000;
  absl::AnyInvocable<uint64_t(absl::Span<const uint64_t>)> last_handler_;
};

TEST(SandboxeeCallbackRegistryTest, NullIsNotACallback) {
  auto* reg = SandboxeeCallbackRegistry::Instance();
  ScopedSandbox scoped;

  // A null callback field stays null rather than becoming a non-null thunk,
  // and consumes no slot. Otherwise every `if (s->cb)` in the library breaks.
  EXPECT_THAT(reg->ThunkFor(scoped.sandbox(), PoolA(), 0), IsOkAndHolds(0));
  EXPECT_EQ(reg->LookUpThunk(scoped.sandbox(), PoolA(), 0), std::nullopt);
}

TEST(SandboxeeCallbackRegistryTest, TheSameFunctionAlwaysGetsTheSameThunk) {
  auto* reg = SandboxeeCallbackRegistry::Instance();
  ScopedSandbox scoped;

  absl::StatusOr<uintptr_t> first =
      reg->ThunkFor(scoped.sandbox(), PoolA(), 0x11);
  ASSERT_THAT(first, IsOk());

  // Otherwise a struct synced twice would burn a slot each time, and the host
  // would hold two addresses that compare unequal but mean one callback.
  EXPECT_THAT(reg->ThunkFor(scoped.sandbox(), PoolA(), 0x11),
              IsOkAndHolds(*first));
}

TEST(SandboxeeCallbackRegistryTest, DifferentFunctionsGetDifferentThunks) {
  auto* reg = SandboxeeCallbackRegistry::Instance();
  ScopedSandbox scoped;

  absl::StatusOr<uintptr_t> one =
      reg->ThunkFor(scoped.sandbox(), PoolA(), 0x11);
  absl::StatusOr<uintptr_t> two =
      reg->ThunkFor(scoped.sandbox(), PoolA(), 0x22);
  ASSERT_THAT(one, IsOk());
  ASSERT_THAT(two, IsOk());

  EXPECT_NE(*one, *two);
}

TEST(SandboxeeCallbackRegistryTest, TheSameAddressInTwoSandboxesIsTwoTargets) {
  auto* reg = SandboxeeCallbackRegistry::Instance();
  ScopedSandbox first;
  ScopedSandbox second;

  // Two sandboxes have independent address spaces, so one number denotes two
  // different functions. A registry keyed on the address alone -- which is what
  // a file-scope slot table amounts to -- would hand both the same thunk and
  // quietly route the second sandbox's callback into the first.
  absl::StatusOr<uintptr_t> in_first =
      reg->ThunkFor(first.sandbox(), PoolA(), 0x11);
  absl::StatusOr<uintptr_t> in_second =
      reg->ThunkFor(second.sandbox(), PoolA(), 0x11);
  ASSERT_THAT(in_first, IsOk());
  ASSERT_THAT(in_second, IsOk());

  EXPECT_NE(*in_first, *in_second);
  EXPECT_EQ(reg->LookUpThunk(first.sandbox(), PoolA(), *in_first), 0x11u);
  EXPECT_EQ(reg->LookUpThunk(second.sandbox(), PoolA(), *in_second), 0x11u);
  EXPECT_DEATH(reg->LookUpThunk(second.sandbox(), PoolA(), *in_first),
               "used with a different sandbox instance");
}

TEST(SandboxeeCallbackRegistryTest, OneFunctionUnderTwoTypedefsIsTwoTargets) {
  auto* reg = SandboxeeCallbackRegistry::Instance();
  ScopedSandbox scoped;

  // Thunks carry their typedef's signature, so one address reached through
  // two callback typedefs needs one thunk out of each pool.
  absl::StatusOr<uintptr_t> as_a =
      reg->ThunkFor(scoped.sandbox(), PoolA(), 0x11);
  absl::StatusOr<uintptr_t> as_b =
      reg->ThunkFor(scoped.sandbox(), PoolB(), 0x11);
  ASSERT_THAT(as_a, IsOk());
  ASSERT_THAT(as_b, IsOk());

  EXPECT_NE(*as_a, *as_b);
  EXPECT_EQ(reg->LookUpThunk(scoped.sandbox(), PoolA(), *as_a), 0x11u);
  EXPECT_EQ(reg->LookUpThunk(scoped.sandbox(), PoolB(), *as_b), 0x11u);
  EXPECT_DEATH(reg->LookUpThunk(scoped.sandbox(), PoolB(), *as_a),
               "Host thunk bound for callback typedef 'byte_sink' used across "
               "a boundary expecting callback typedef 'chunk_allocator'");
}

TEST(SandboxeeCallbackRegistryTest, LookUpThunkRoundTripsAndRejectsStrangers) {
  auto* reg = SandboxeeCallbackRegistry::Instance();
  ScopedSandbox scoped;

  absl::StatusOr<uintptr_t> thunk =
      reg->ThunkFor(scoped.sandbox(), PoolA(), 0x11);
  ASSERT_THAT(thunk, IsOk());

  EXPECT_EQ(reg->LookUpThunk(scoped.sandbox(), PoolA(), *thunk), 0x11u);

  // A genuine host function is simply absent, which is how a caller tells the
  // two apart without an equality scan over the whole thunk table.
  EXPECT_EQ(reg->LookUpThunk(scoped.sandbox(), PoolA(),
                             reinterpret_cast<uintptr_t>(&ThunkAddress)),
            std::nullopt);
}

TEST(SandboxeeCallbackRegistryTest, ExhaustionIsLoudRatherThanReusingASlot) {
  auto* reg = SandboxeeCallbackRegistry::Instance();
  ScopedSandbox scoped;

  ASSERT_THAT(reg->ThunkFor(scoped.sandbox(), PoolB(), 0x11), IsOk());
  ASSERT_THAT(reg->ThunkFor(scoped.sandbox(), PoolB(), 0x22), IsOk());

  // PoolB has two slots. Handing out a third by evicting one would silently
  // redirect a callback the host still holds, so this has to be an error.
  EXPECT_THAT(reg->ThunkFor(scoped.sandbox(), PoolB(), 0x33),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("chunk_allocator")));
}

TEST(SandboxeeCallbackRegistryTest, ReleaseReturnsTheSlotToThePool) {
  auto* reg = SandboxeeCallbackRegistry::Instance();
  ScopedSandbox scoped;

  absl::StatusOr<uintptr_t> first =
      reg->ThunkFor(scoped.sandbox(), PoolB(), 0x11);
  ASSERT_THAT(first, IsOk());
  ASSERT_THAT(reg->ThunkFor(scoped.sandbox(), PoolB(), 0x22), IsOk());
  ASSERT_THAT(reg->ThunkFor(scoped.sandbox(), PoolB(), 0x33),
              StatusIs(absl::StatusCode::kResourceExhausted));

  reg->Release(*first);

  EXPECT_EQ(reg->LookUpThunk(scoped.sandbox(), PoolB(), *first), std::nullopt);
  // Re-registering the same target after Release allocates the freed slot.
  EXPECT_THAT(reg->ThunkFor(scoped.sandbox(), PoolB(), 0x11),
              IsOkAndHolds(*first));
}

TEST(SandboxeeCallbackRegistryTest,
     ReleaseNoOpsOnZeroAndCheckFailsOnUnknownThunk) {
  auto* reg = SandboxeeCallbackRegistry::Instance();

  reg->Release(0);
  EXPECT_DEATH(
      reg->Release(reinterpret_cast<uintptr_t>(&ThunkAddress)),
      "Attempted to release an unknown or already released host thunk");
}

TEST(SandboxeeCallbackRegistryTest, ForgetSandboxReleasesOnlyItsOwnSlots) {
  auto* reg = SandboxeeCallbackRegistry::Instance();
  ScopedSandbox keeper;

  absl::StatusOr<uintptr_t> kept =
      reg->ThunkFor(keeper.sandbox(), PoolA(), 0x11);
  ASSERT_THAT(kept, IsOk());

  uintptr_t doomed_thunk = 0;
  {
    ScopedSandbox doomed;
    absl::StatusOr<uintptr_t> got =
        reg->ThunkFor(doomed.sandbox(), PoolA(), 0x22);
    ASSERT_THAT(got, IsOk());
    ASSERT_NE(*got, *kept);
    doomed_thunk = *got;
  }  // ScopedSandbox's destructor calls ForgetSandbox.

  EXPECT_EQ(reg->LookUpThunk(keeper.sandbox(), PoolA(), doomed_thunk),
            std::nullopt);
  EXPECT_EQ(reg->LookUpThunk(keeper.sandbox(), PoolA(), *kept), 0x11u);

  // The forgotten slot is reusable, and the surviving binding still dedups.
  EXPECT_THAT(reg->ThunkFor(keeper.sandbox(), PoolA(), 0x11),
              IsOkAndHolds(*kept));
}

TEST(CheckedToSizeTTest, ConvertsNonNegativeAndRejectsNegativeSignedValues) {
  EXPECT_EQ(CheckedToSizeT(0), 0u);
  EXPECT_EQ(CheckedToSizeT(42), 42u);
  EXPECT_EQ(CheckedToSizeT(size_t{0}), 0u);
  EXPECT_EQ(CheckedToSizeT(size_t{99}), 99u);
  EXPECT_DEATH(CheckedToSizeT(-1), "Negative buffer size: -1");
  EXPECT_DEATH(CheckedToSizeT(int64_t{-8}), "Negative buffer size: -8");
}

// =========================================================================
// HostCallbackRegistry Tests
// =========================================================================

TEST(HostCallbackRegistryTest, NullIsNotACallback) {
  auto* reg = HostCallbackRegistry::Instance();
  ScopedSandbox scoped;

  EXPECT_CALL(scoped.channel(), RegisterCallback(_)).Times(0);
  EXPECT_THAT(reg->TrampolineFor(scoped.sandbox(), "adder", 0,
                                 [](int a, int b) { return a + b; }),
              IsOkAndHolds(0));
  EXPECT_EQ(reg->LookUpTrampoline(scoped.sandbox(), "adder", 0), std::nullopt);
}

TEST(HostCallbackRegistryTest, TheSameFunctionAlwaysGetsTheSameTrampoline) {
  auto* reg = HostCallbackRegistry::Instance();
  ScopedSandbox scoped;

  EXPECT_CALL(scoped.channel(), RegisterCallback(_)).Times(1);
  absl::StatusOr<uintptr_t> first = reg->TrampolineFor(
      scoped.sandbox(), "adder", 0x1111, [](int a, int b) { return a + b; });
  ASSERT_THAT(first, IsOk());

  // Installing the same host callback twice reuses one trampoline slot and
  // hands the sandboxee the same address so pointer equality holds.
  EXPECT_THAT(reg->TrampolineFor(scoped.sandbox(), "adder", 0x1111,
                                 [](int a, int b) { return a + b; }),
              IsOkAndHolds(*first));
}

TEST(HostCallbackRegistryTest, DifferentFunctionsGetDifferentTrampolines) {
  auto* reg = HostCallbackRegistry::Instance();
  ScopedSandbox scoped;

  absl::StatusOr<uintptr_t> one = reg->TrampolineFor(
      scoped.sandbox(), "adder", 0x1111, [](int a, int b) { return a + b; });
  absl::StatusOr<uintptr_t> two = reg->TrampolineFor(
      scoped.sandbox(), "adder", 0x2222, [](int a, int b) { return a * b; });
  ASSERT_THAT(one, IsOk());
  ASSERT_THAT(two, IsOk());

  EXPECT_NE(*one, *two);
}

TEST(HostCallbackRegistryTest,
     TheSameFunctionInTwoSandboxesGetsOneTrampolineInEach) {
  auto* reg = HostCallbackRegistry::Instance();
  ScopedSandbox first;
  ScopedSandbox second;

  EXPECT_CALL(first.channel(), RegisterCallback(_)).Times(1);
  EXPECT_CALL(second.channel(), RegisterCallback(_)).Times(1);

  absl::StatusOr<uintptr_t> in_first = reg->TrampolineFor(
      first.sandbox(), "adder", 0x1111, [](int a, int b) { return a + b; });
  absl::StatusOr<uintptr_t> in_second = reg->TrampolineFor(
      second.sandbox(), "adder", 0x1111, [](int a, int b) { return a + b; });
  ASSERT_THAT(in_first, IsOk());
  ASSERT_THAT(in_second, IsOk());

  EXPECT_EQ(reg->LookUpTrampoline(first.sandbox(), "adder", *in_first),
            0x1111u);
  EXPECT_EQ(reg->LookUpTrampoline(second.sandbox(), "adder", *in_second),
            0x1111u);
}

TEST(HostCallbackRegistryTest, OneFunctionUnderTwoTypedefsGetsTwoTrampolines) {
  auto* reg = HostCallbackRegistry::Instance();
  ScopedSandbox scoped;

  // Each callback typedef has its own parameter marshaling wrapper, so one host
  // function address used under two callback typedefs needs a distinct
  // trampoline for each.
  absl::StatusOr<uintptr_t> as_a =
      reg->TrampolineFor(scoped.sandbox(), "typedef_a", 0x1111,
                         [](int a, int b) { return a + b; });
  absl::StatusOr<uintptr_t> as_b =
      reg->TrampolineFor(scoped.sandbox(), "typedef_b", 0x1111,
                         [](int a, int b) { return a + b; });
  ASSERT_THAT(as_a, IsOk());
  ASSERT_THAT(as_b, IsOk());

  EXPECT_NE(*as_a, *as_b);
  EXPECT_EQ(reg->LookUpTrampoline(scoped.sandbox(), "typedef_a", *as_a),
            0x1111u);
  EXPECT_EQ(reg->LookUpTrampoline(scoped.sandbox(), "typedef_b", *as_b),
            0x1111u);
}

TEST(HostCallbackRegistryTest,
     LookUpTrampolineRejectsCrossTypedefSubstitution) {
  auto* reg = HostCallbackRegistry::Instance();
  ScopedSandbox scoped;

  absl::StatusOr<uintptr_t> as_a =
      reg->TrampolineFor(scoped.sandbox(), "typedef_a", 0x1111,
                         [](int a, int b) { return a + b; });
  ASSERT_THAT(as_a, IsOk());

  // Security check: if a compromised sandboxee returns `as_a` across a
  // boundary governed by `typedef_b`, LookUpTrampoline must CHECK-fail rather
  // than unwrap `0x1111` under the wrong C ABI signature.
  EXPECT_DEATH(reg->LookUpTrampoline(scoped.sandbox(), "typedef_b", *as_a),
               "registered for callback typedef 'typedef_a' across a boundary "
               "expecting callback typedef 'typedef_b'");
}

TEST(HostCallbackRegistryTest, LookUpTrampolineRoundTripsAndRejectsStrangers) {
  auto* reg = HostCallbackRegistry::Instance();
  ScopedSandbox scoped;

  absl::StatusOr<uintptr_t> trampoline = reg->TrampolineFor(
      scoped.sandbox(), "adder", 0x1111, [](int a, int b) { return a + b; });
  ASSERT_THAT(trampoline, IsOk());

  EXPECT_EQ(reg->LookUpTrampoline(scoped.sandbox(), "adder", *trampoline),
            0x1111u);
  // A genuine sandboxee function address is absent, telling the caller to bind
  // a host thunk from SandboxeeCallbackRegistry instead.
  EXPECT_EQ(reg->LookUpTrampoline(scoped.sandbox(), "adder", 0x9999),
            std::nullopt);
}

TEST(HostCallbackRegistryTest,
     RegisteredHandlerRemainsCallableAfterRegistrationReturns) {
  auto* reg = HostCallbackRegistry::Instance();
  ScopedSandbox scoped;

  ASSERT_THAT(reg->TrampolineFor(scoped.sandbox(), "adder", 0x1111,
                                 [](int a, int b) { return a + b; }),
              IsOk());

  const uint64_t kArgs[] = {17, 25};
  EXPECT_EQ(scoped.InvokeLastHandler(kArgs), 42u);

  // By default (`idempotent == false`), a second invocation from the sandboxee
  // without the host re-registering the callback CHECK-fails.
  EXPECT_DEATH(
      scoped.InvokeLastHandler(kArgs),
      "was invoked more than once without SANDBOX_CALLBACK_IDEMPOTENT");

  // Re-registering the callback from the host re-arms the existing trampoline
  // slot without allocating a second slot.
  ASSERT_THAT(reg->TrampolineFor(scoped.sandbox(), "adder", 0x1111,
                                 [](int a, int b) { return a + b; }),
              IsOk());
  EXPECT_EQ(scoped.InvokeLastHandler(kArgs), 42u);
}

TEST(HostCallbackRegistryTest,
     IdempotentCallbackAllowsRepeatedInvocationsWithoutReArming) {
  auto* reg = HostCallbackRegistry::Instance();
  ScopedSandbox scoped;

  ASSERT_THAT(reg->TrampolineFor(
                  scoped.sandbox(), "adder_multi", 0x1111,
                  [](int a, int b) { return a + b; }, /*idempotent=*/true),
              IsOk());

  const uint64_t kArgs1[] = {10, 20};
  const uint64_t kArgs2[] = {30, 40};
  EXPECT_EQ(scoped.InvokeLastHandler(kArgs1), 30u);
  EXPECT_EQ(scoped.InvokeLastHandler(kArgs2), 70u);

  // Re-registering the same target as single-shot (`idempotent = false`)
  // updates the cached binding's idempotence policy.
  ASSERT_THAT(reg->TrampolineFor(
                  scoped.sandbox(), "adder_multi", 0x1111,
                  [](int a, int b) { return a + b; }, /*idempotent=*/false),
              IsOk());
  EXPECT_EQ(scoped.InvokeLastHandler(kArgs1), 30u);
  EXPECT_DEATH(
      scoped.InvokeLastHandler(kArgs2),
      "was invoked more than once without SANDBOX_CALLBACK_IDEMPOTENT");
}

TEST(HostCallbackRegistryTest,
     CallScopedVarCallbackEnforcesSingleShotWhenNotIdempotent) {
  ScopedSandbox scoped;

  sapi::v::Callback single_shot([](int a, int b) { return a + b; },
                                /*idempotent=*/false);
  ASSERT_THAT(scoped.sandbox().Allocate(&single_shot, /*automatic_free=*/false),
              IsOk());

  const uint64_t kArgs1[] = {12, 30};
  const uint64_t kArgs2[] = {5, 6};
  EXPECT_EQ(scoped.InvokeLastHandler(kArgs1), 42u);
  EXPECT_DEATH(
      scoped.InvokeLastHandler(kArgs2),
      "was invoked more than once without SANDBOX_CALLBACK_IDEMPOTENT");

  sapi::v::Callback idempotent_cb([](int a, int b) { return a * b; },
                                  /*idempotent=*/true);
  ASSERT_THAT(
      scoped.sandbox().Allocate(&idempotent_cb, /*automatic_free=*/false),
      IsOk());
  EXPECT_EQ(scoped.InvokeLastHandler(kArgs1), 360u);
  EXPECT_EQ(scoped.InvokeLastHandler(kArgs2), 30u);
}

TEST(HostCallbackRegistryTest, ExhaustionPropagatesFromChannel) {
  auto* reg = HostCallbackRegistry::Instance();
  ScopedSandbox scoped;

  EXPECT_CALL(scoped.channel(), RegisterCallback(_))
      .WillOnce(Return(absl::ResourceExhaustedError("No free callback slots")));
  EXPECT_THAT(reg->TrampolineFor(scoped.sandbox(), "adder", 0x1111,
                                 [](int a, int b) { return a + b; }),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("No free callback slots")));
}

TEST(HostCallbackRegistryTest, ReleaseUnregistersAndFreesTheTrampoline) {
  auto* reg = HostCallbackRegistry::Instance();
  ScopedSandbox scoped;

  absl::StatusOr<uintptr_t> trampoline = reg->TrampolineFor(
      scoped.sandbox(), "adder", 0x1111, [](int a, int b) { return a + b; });
  ASSERT_THAT(trampoline, IsOk());

  EXPECT_CALL(scoped.channel(), UnregisterCallback(*trampoline))
      .WillOnce(Return(absl::OkStatus()));
  reg->Release(scoped.sandbox(), *trampoline);
  testing::Mock::VerifyAndClearExpectations(&scoped.channel());

  EXPECT_EQ(reg->LookUpTrampoline(scoped.sandbox(), "adder", *trampoline),
            std::nullopt);
  // Safe no-op on 0, and CHECK-failure on unknown or already released
  // trampolines.
  reg->Release(scoped.sandbox(), 0);
  EXPECT_DEATH(reg->Release(scoped.sandbox(), *trampoline),
               "Attempted to release an unknown or already released host "
               "callback trampoline");

  // Re-registering the same target after Release registers a new trampoline.
  absl::StatusOr<uintptr_t> re_registered = reg->TrampolineFor(
      scoped.sandbox(), "adder", 0x1111, [](int a, int b) { return a + b; });
  ASSERT_THAT(re_registered, IsOk());
  EXPECT_NE(*re_registered, *trampoline);
}

TEST(HostCallbackRegistryTest, ForgetSandboxReleasesOnlyItsOwnTrampolines) {
  auto* reg = HostCallbackRegistry::Instance();
  ScopedSandbox keeper;

  absl::StatusOr<uintptr_t> kept = reg->TrampolineFor(
      keeper.sandbox(), "adder", 0x1111, [](int a, int b) { return a + b; });
  ASSERT_THAT(kept, IsOk());

  ScopedSandbox doomed;
  absl::StatusOr<uintptr_t> got = reg->TrampolineFor(
      doomed.sandbox(), "adder", 0x2222, [](int a, int b) { return a * b; });
  ASSERT_THAT(got, IsOk());
  uintptr_t doomed_trampoline = *got;

  EXPECT_CALL(doomed.channel(), UnregisterCallback(doomed_trampoline))
      .WillOnce(Return(absl::OkStatus()));
  reg->ForgetSandbox(doomed.sandbox());
  testing::Mock::VerifyAndClearExpectations(&doomed.channel());

  EXPECT_EQ(reg->LookUpTrampoline(doomed.sandbox(), "adder", doomed_trampoline),
            std::nullopt);
  EXPECT_EQ(reg->LookUpTrampoline(keeper.sandbox(), "adder", *kept), 0x1111u);
  EXPECT_THAT(reg->TrampolineFor(keeper.sandbox(), "adder", 0x1111,
                                 [](int a, int b) { return a + b; }),
              IsOkAndHolds(*kept));
  absl::StatusOr<uintptr_t> re_registered = reg->TrampolineFor(
      doomed.sandbox(), "adder", 0x2222, [](int a, int b) { return a * b; });
  ASSERT_THAT(re_registered, IsOk());
  EXPECT_NE(*re_registered, doomed_trampoline);
}

TEST(HostCallbackRegistryTest,
     RestartingSandboxWithoutForgetSandboxInvalidatesStaleBindings) {
  auto* host_reg = HostCallbackRegistry::Instance();
  auto* sb_reg = SandboxeeCallbackRegistry::Instance();

  testing::NiceMock<MockRPCChannel> first_channel;
  ON_CALL(first_channel, RegisterCallback(_)).WillByDefault(Return(0x7100));
  testing::NiceMock<MockRPCChannel> second_channel;
  ON_CALL(second_channel, RegisterCallback(_)).WillByDefault(Return(0x8200));

  MockSandbox sandbox(&first_channel);

  absl::StatusOr<uintptr_t> first_trampoline = host_reg->TrampolineFor(
      sandbox, "adder", 0x1111, [](int a, int b) { return a + b; });
  ASSERT_THAT(first_trampoline, IsOkAndHolds(0x7100));
  EXPECT_EQ(host_reg->LookUpTrampoline(sandbox, "adder", 0x7100), 0x1111u);

  // Exhaust all 3 slots in PoolA during the first session.
  absl::StatusOr<uintptr_t> first_thunk =
      sb_reg->ThunkFor(sandbox, PoolA(), 0x10);
  ASSERT_THAT(first_thunk, IsOk());
  ASSERT_THAT(sb_reg->ThunkFor(sandbox, PoolA(), 0x20), IsOk());
  ASSERT_THAT(sb_reg->ThunkFor(sandbox, PoolA(), 0x30), IsOk());

  // Simulate restarting `sandbox` in place (replacing its RPCChannel without
  // calling `ForgetSandbox`). `second_channel` has a distinct `session_id()`.
  sandbox.set_channel(&second_channel);

  // Stale trampoline and thunk bindings from the old channel must no longer
  // unwrap, and `second_channel` must receive a fresh `RegisterCallback` call.
  EXPECT_EQ(sb_reg->LookUpThunk(sandbox, PoolA(), *first_thunk), std::nullopt);
  EXPECT_EQ(host_reg->LookUpTrampoline(sandbox, "adder", 0x7100), std::nullopt);
  EXPECT_CALL(second_channel, RegisterCallback(_)).WillOnce(Return(0x8200));
  absl::StatusOr<uintptr_t> second_trampoline = host_reg->TrampolineFor(
      sandbox, "adder", 0x1111, [](int a, int b) { return a + b; });
  ASSERT_THAT(second_trampoline, IsOkAndHolds(0x8200));
  EXPECT_EQ(host_reg->LookUpTrampoline(sandbox, "adder", 0x8200), 0x1111u);

  // PoolA slots from the previous session must also be reclaimed automatically.
  EXPECT_THAT(sb_reg->ThunkFor(sandbox, PoolA(), 0x40), IsOk());

  sb_reg->ForgetSandbox(sandbox);
  host_reg->ForgetSandbox(sandbox);
}

TEST(HostCallbackRegistryTest,
     SessionInvalidatedMidRegistrationUnregistersSlotAndReturnsUnavailable) {
  auto* host_reg = HostCallbackRegistry::Instance();

  testing::NiceMock<MockRPCChannel> first_channel;
  testing::NiceMock<MockRPCChannel> second_channel;
  MockSandbox sandbox(&first_channel);

  EXPECT_CALL(first_channel, RegisterCallback(_)).WillOnce([&](auto) {
    // Simulate a concurrent sandbox restart advancing `active_session_` while
    // `first_channel.RegisterCallback` is in flight outside `mutex_`.
    sandbox.set_channel(&second_channel);
    EXPECT_CALL(second_channel, RegisterCallback(_)).WillOnce(Return(0x8200));
    EXPECT_THAT(host_reg->TrampolineFor(sandbox, "adder", 0x2222,
                                        [](int a, int b) { return a * b; }),
                IsOkAndHolds(0x8200));
    return 0x7100;
  });
  EXPECT_CALL(first_channel, UnregisterCallback(0x7100))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_THAT(
      host_reg->TrampolineFor(sandbox, "adder", 0x1111,
                              [](int a, int b) { return a + b; }),
      StatusIs(absl::StatusCode::kUnavailable, HasSubstr("invalidated")));

  host_reg->ForgetSandbox(sandbox);
}

TEST(HostCallbackRegistryTest,
     ForgetSandboxAndReleaseHandleTerminatedSandboxWithNullChannel) {
  auto* reg = HostCallbackRegistry::Instance();
  testing::NiceMock<MockRPCChannel> channel;
  EXPECT_CALL(channel, RegisterCallback(_))
      .WillOnce(Return(0x7300))
      .WillOnce(Return(0x7310));
  MockSandbox sandbox(&channel);

  ASSERT_THAT(reg->TrampolineFor(sandbox, "adder", 0x1111,
                                 [](int a, int b) { return a + b; }),
              IsOkAndHolds(0x7300));
  ASSERT_THAT(reg->TrampolineFor(sandbox, "adder", 0x2222,
                                 [](int a, int b) { return a * b; }),
              IsOkAndHolds(0x7310));

  // Simulate `sandbox.Terminate()` clearing `rpc_channel()` to nullptr before
  // `Release` and `ForgetSandbox` run.
  sandbox.set_channel(nullptr);
  EXPECT_EQ(reg->LookUpTrampoline(sandbox, "adder", 0x7300), std::nullopt);
  reg->Release(sandbox, 0x7300);
  reg->ForgetSandbox(sandbox);
}

TEST(HostCallbackRegistryTest,
     ConcurrentThreadsShareTrampolinesAndThunksSafely) {
  auto* host_reg = HostCallbackRegistry::Instance();
  auto* sb_reg = SandboxeeCallbackRegistry::Instance();
  ScopedSandbox scoped;

  constexpr int kThreads = 8;
  std::vector<uintptr_t> trampolines(kThreads, 0);
  std::vector<uintptr_t> thunks(kThreads, 0);
  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int i = 0; i < kThreads; ++i) {
    workers.emplace_back([&, i] {
      absl::StatusOr<uintptr_t> tramp =
          host_reg->TrampolineFor(scoped.sandbox(), "adder", 0x1111,
                                  [](int a, int b) { return a + b; });
      ASSERT_THAT(tramp, IsOk());
      trampolines[i] = *tramp;
      EXPECT_EQ(host_reg->LookUpTrampoline(scoped.sandbox(), "adder", *tramp),
                0x1111u);

      absl::StatusOr<uintptr_t> thunk =
          sb_reg->ThunkFor(scoped.sandbox(), PoolA(), 0x5555);
      ASSERT_THAT(thunk, IsOk());
      thunks[i] = *thunk;
      EXPECT_EQ(sb_reg->LookUpThunk(scoped.sandbox(), PoolA(), *thunk),
                0x5555u);
    });
  }
  for (std::thread& t : workers) {
    t.join();
  }
  for (int i = 1; i < kThreads; ++i) {
    EXPECT_EQ(trampolines[i], trampolines[0]);
    EXPECT_EQ(thunks[i], thunks[0]);
  }
}

uint64_t NoexceptCallbackFn(int x) noexcept { return x + 10; }

TEST(HostCallbackRegistryTest, SupportsNoexceptFunctionPointersAndLambdas) {
  ScopedSandbox scoped;
  auto* reg = HostCallbackRegistry::Instance();

  absl::StatusOr<uintptr_t> tramp = reg->TrampolineFor(
      scoped.sandbox(), "noexcept_cb",
      reinterpret_cast<uintptr_t>(&NoexceptCallbackFn), &NoexceptCallbackFn);
  ASSERT_THAT(tramp, IsOk());
  const uint64_t kArgs[1] = {32};
  EXPECT_EQ(scoped.InvokeLastHandler(kArgs), 42u);

  sapi::v::Callback var_cb([](int a, const int* const& p) noexcept -> int {
    return a + (p != nullptr ? *p : 0);
  });
  EXPECT_EQ(var_cb.GetSize(), sizeof(void*));
}

// A channel that passes fewer slots than `CallbackRequest::args` (for example
// one that passes only the 6 integer argument registers) uses the flat layout.
TEST(HostCallbackRegistryTest, FlatLayoutForwardsIntegerArgumentsInOrder) {
  ScopedSandbox scoped;
  auto* reg = HostCallbackRegistry::Instance();
  ASSERT_THAT(reg->TrampolineFor(scoped.sandbox(), "flat_six", 0x6666,
                                 [](int a, int b, int c, int d, int e, int f) {
                                   return a + 2 * b + 3 * c + 4 * d + 5 * e +
                                          6 * f;
                                 }),
              IsOk());
  const uint64_t kArgs[6] = {1, 2, 3, 4, 5, 6};
  EXPECT_EQ(scoped.InvokeLastHandler(kArgs), 91u);
}

TEST(HostCallbackRegistryTest, FlatLayoutRejectsCallbacksThatDoNotFit) {
  const uint64_t kArgs[6] = {1, 2, 3, 4, 5, 6};
  {
    ScopedSandbox scoped;
    ASSERT_THAT(HostCallbackRegistry::Instance()->TrampolineFor(
                    scoped.sandbox(), "flat_seven", 0x7777,
                    [](int, int, int, int, int, int, int) { return 0; }),
                IsOk());
    EXPECT_DEATH(scoped.InvokeLastHandler(kArgs),
                 "Callback takes 7 arguments, but the RPC channel passes only "
                 "6");
  }
  {
    ScopedSandbox scoped;
    ASSERT_THAT(HostCallbackRegistry::Instance()->TrampolineFor(
                    scoped.sandbox(), "flat_double_arg", 0x8888,
                    [](int a, double b) { return a + static_cast<int>(b); }),
                IsOk());
    EXPECT_DEATH(scoped.InvokeLastHandler(kArgs),
                 "float or double arguments or return values");
  }
  {
    ScopedSandbox scoped;
    ASSERT_THAT(HostCallbackRegistry::Instance()->TrampolineFor(
                    scoped.sandbox(), "flat_float_ret", 0x9999,
                    [](int a) { return static_cast<float>(a); }),
                IsOk());
    EXPECT_DEATH(scoped.InvokeLastHandler(kArgs),
                 "float or double arguments or return values");
  }
}

TEST(HostCallbackRegistryTest, StackArgCountMatchesTargetAbi) {
  using sapi::v::callback_internal::StackArgCount;
  EXPECT_EQ(StackArgCount<std::tuple<>>(), 0u);
  EXPECT_EQ((StackArgCount<std::tuple<int, int, int, int, int, int>>()), 0u);
  EXPECT_EQ((StackArgCount<std::tuple<double, double, double, double, double,
                                      double, double, double>>()),
            0u);
#if defined(__riscv) && defined(__riscv_float_abi_double)
  // When all 8 floating-point argument registers are in use, RISC-V passes the
  // next `float` in an integer register, not on the stack.
  EXPECT_EQ((StackArgCount<std::tuple<float, float, float, float, float, float,
                                      float, float, float, int>>()),
            0u);
#elif !defined(__riscv)
  EXPECT_EQ((StackArgCount<std::tuple<float, float, float, float, float, float,
                                      float, float, float, int>>()),
            1u);
#endif
#if defined(__aarch64__) || defined(__riscv)
  EXPECT_EQ(
      (StackArgCount<std::tuple<int, int, int, int, int, int, int, int>>()),
      0u);
  EXPECT_EQ(
      (StackArgCount<
          std::tuple<int, int, int, int, int, int, int, int, int, int>>()),
      2u);
#else
  EXPECT_EQ((StackArgCount<std::tuple<int, int, int, int, int, int, int>>()),
            1u);
  EXPECT_EQ(
      (StackArgCount<std::tuple<int, int, int, int, int, int, int, int>>()),
      2u);
  EXPECT_EQ((StackArgCount<
                std::tuple<int, int, int, int, int, int, int, int, int>>()),
            3u);
  EXPECT_EQ(
      (StackArgCount<
          std::tuple<int, int, int, int, int, int, int, int, int, int>>()),
      4u);
#endif
}

// =========================================================================
// NullTerminatedByteSize & ReadNullTerminatedArray Tests
// =========================================================================

enum TestOpCode : int {
  kOpEnd = 0,
  kOpLoad = 1,
  kOpStore = 2,
};

TEST(NullTerminatedArrayTest,
     NullTerminatedCountAndByteSizeHandleNullAndArrays) {
  const int* null_ints = nullptr;
  EXPECT_EQ(NullTerminatedCount(null_ints), 0u);
  EXPECT_EQ(NullTerminatedByteSize(null_ints), 0u);

  // libxml2 `const xmlChar*` (`const unsigned char*`) UTF-8 sequence.
  const unsigned char kXmlTag[] = {'n', 'o', 'd', 'e', 0};
  EXPECT_EQ(NullTerminatedCount(kXmlTag), 4u);
  EXPECT_EQ(NullTerminatedByteSize(kXmlTag), 5 * sizeof(unsigned char));

  // SQLite `sqlite3_open16` `const char16_t*` UTF-16 sequence.
  const char16_t kUtf16Path[] = {u'm', u'e', u'm', 0};
  EXPECT_EQ(NullTerminatedCount(kUtf16Path), 3u);
  EXPECT_EQ(NullTerminatedByteSize(kUtf16Path), 4 * sizeof(char16_t));

  // Zero-terminated integer and enum sequences.
  const int kEmptyInts[] = {0};
  EXPECT_EQ(NullTerminatedCount(kEmptyInts), 0u);
  EXPECT_EQ(NullTerminatedByteSize(kEmptyInts), sizeof(int));

  const int kInts[] = {10, 20, 30, 0};
  EXPECT_EQ(NullTerminatedCount(kInts), 3u);
  EXPECT_EQ(NullTerminatedByteSize(kInts), 4 * sizeof(int));

  const TestOpCode kOps[] = {kOpLoad, kOpStore, kOpEnd};
  EXPECT_EQ(NullTerminatedCount(kOps), 2u);
  EXPECT_EQ(NullTerminatedByteSize(kOps), 3 * sizeof(TestOpCode));

  // Lua `luaL_checkoption` `const char* const lst[]` pointer array.
  const char* const kOptions[] = {"none", "fast", "full", nullptr};
  EXPECT_EQ(NullTerminatedCount(kOptions), 3u);
  EXPECT_EQ(NullTerminatedByteSize(kOptions), 4 * sizeof(const char*));

  // Lua `luaL_Reg`-like struct array: an entry with only some zero fields
  // (`{"placeholder", nullptr}`) does not end the array; the all-zero
  // `{nullptr, nullptr}` sentinel does.
  struct TestLuaReg {
    const char* name;
    int (*func)(void*);
  };
  const TestLuaReg kRegs[] = {
      {"add", +[](void*) { return 1; }},
      {"placeholder", nullptr},
      {nullptr, nullptr},
  };
  EXPECT_EQ(NullTerminatedCount(kRegs), 2u);
  EXPECT_EQ(NullTerminatedByteSize(kRegs), 3 * sizeof(TestLuaReg));
}

TEST(NullTerminatedArrayTest, ReadNullTerminatedArrayHandlesNullAndByteArrays) {
  MockRPCChannel mock_channel;
  MockSandbox sandbox(&mock_channel);

  EXPECT_TRUE(ReadNullTerminatedArray(sandbox, 0, sizeof(int)).empty());

  void* fake_remote = reinterpret_cast<void*>(0xCC00);
  const std::string kBytes = "xml";
  EXPECT_CALL(mock_channel, Strlen(fake_remote))
      .WillOnce(Return(kBytes.size()));
  EXPECT_CALL(mock_channel, CopyFromSandbox(0xCC00, _))
      .WillOnce([&](uintptr_t, absl::Span<char> dst) {
        std::memcpy(dst.data(), kBytes.data(), kBytes.size());
        return kBytes.size();
      });

  std::vector<char> buf = ReadNullTerminatedArray(sandbox, 0xCC00, 1);
  ASSERT_EQ(buf.size(), 4u);
  EXPECT_STREQ(buf.data(), "xml");
}

TEST(NullTerminatedArrayTest, ReadNullTerminatedArrayReadsMultiByteElements) {
  MockRPCChannel mock_channel;
  MockSandbox sandbox(&mock_channel);

  // All 3 elements lie within the same 4 KiB page (`0xDD00..0xE000`, 768 bytes
  // remaining in page), so `ReadNullTerminatedArray` must fetch them in a
  // single page-bounded `CopyFromSandbox` call rather than 3 per-element calls.
  const uint16_t kUtf16[] = {0x0100, 0x03B1, 0x0000};
  EXPECT_CALL(mock_channel, CopyFromSandbox(0xDD00, _))
      .WillOnce([&](uintptr_t src,
                    absl::Span<char> dst) -> absl::StatusOr<size_t> {
        EXPECT_EQ(dst.size(), 4096u - (0xDD00u & 0xFFFu));
        size_t offset = src - 0xDD00;
        size_t to_copy = std::min(dst.size(), sizeof(kUtf16) - offset);
        std::memcpy(dst.data(), reinterpret_cast<const char*>(kUtf16) + offset,
                    to_copy);
        return to_copy;
      });

  std::vector<char> buf =
      ReadNullTerminatedArray(sandbox, 0xDD00, sizeof(uint16_t));
  ASSERT_EQ(buf.size(), sizeof(kUtf16));
  const auto* words = reinterpret_cast<const uint16_t*>(buf.data());
  EXPECT_EQ(words[0], 0x0100);
  EXPECT_EQ(words[1], 0x03B1);
  EXPECT_EQ(words[2], 0x0000);
}

TEST(NullTerminatedArrayTest,
     ReadNullTerminatedArraySplitsBatchesAtPageBoundary) {
  MockRPCChannel mock_channel;
  MockSandbox sandbox(&mock_channel);

  // Place a 4-element `uint32_t` array starting 8 bytes before the end of page
  // `0x1000..0x2000` (`0x1FF8`). The first batch must clamp to the 2 elements
  // (`8` bytes) before `0x2000`, and the second batch reads starting at
  // `0x2000` where the sentinel resides.
  const uint32_t kWords[] = {11, 22, 33, 0};
  constexpr uintptr_t kStartAddr = 0x1FF8;
  EXPECT_CALL(mock_channel, CopyFromSandbox(kStartAddr, _))
      .WillOnce([&](uintptr_t, absl::Span<char> dst) -> absl::StatusOr<size_t> {
        EXPECT_EQ(dst.size(), 8u);
        std::memcpy(dst.data(), kWords, 8);
        return 8u;
      });
  EXPECT_CALL(mock_channel, CopyFromSandbox(0x2000, _))
      .WillOnce([&](uintptr_t, absl::Span<char> dst) -> absl::StatusOr<size_t> {
        EXPECT_EQ(dst.size(), 4096u);
        std::memcpy(dst.data(), kWords + 2, 8);
        return 8u;
      });

  std::vector<char> buf =
      ReadNullTerminatedArray(sandbox, kStartAddr, sizeof(uint32_t));
  ASSERT_EQ(buf.size(), sizeof(kWords));
  const auto* words = reinterpret_cast<const uint32_t*>(buf.data());
  EXPECT_EQ(words[0], 11u);
  EXPECT_EQ(words[1], 22u);
  EXPECT_EQ(words[2], 33u);
  EXPECT_EQ(words[3], 0u);
}

TEST(NullTerminatedArrayTest,
     ReadNullTerminatedArrayFallsBackToSingleElementModeOnBoundedAllocation) {
  MockRPCChannel mock_channel;
  MockSandbox sandbox(&mock_channel);

  // When `remote_ptr` resides inside `SharedMemoryRPCChannel`,
  // `EnsureWithinAllocationBounds` rejects reads larger than the exact
  // allocation size (`sizeof(kUtf16)`). Verify that `ReadNullTerminatedArray`
  // falls back to single-element reads and succeeds.
  const uint16_t kUtf16[] = {0x0041, 0x03B1, 0x0000};
  EXPECT_CALL(mock_channel, CopyFromSandbox(_, _))
      .WillRepeatedly([&](uintptr_t src,
                          absl::Span<char> dst) -> absl::StatusOr<size_t> {
        size_t offset = src - 0xDD00;
        if (offset + dst.size() > sizeof(kUtf16)) {
          return absl::InternalError("Range is out of bounds.");
        }
        std::memcpy(dst.data(), reinterpret_cast<const char*>(kUtf16) + offset,
                    dst.size());
        return dst.size();
      });

  std::vector<char> buf =
      ReadNullTerminatedArray(sandbox, 0xDD00, sizeof(uint16_t));
  ASSERT_EQ(buf.size(), sizeof(kUtf16));
  const auto* words = reinterpret_cast<const uint16_t*>(buf.data());
  EXPECT_EQ(words[0], 0x0041);
  EXPECT_EQ(words[1], 0x03B1);
  EXPECT_EQ(words[2], 0x0000);
}

TEST(NullTerminatedArrayTest,
     ReadNullTerminatedArrayTypedOverloadIgnoresStructSentinelPaddingBytes) {
  MockRPCChannel mock_channel;
  MockSandbox sandbox(&mock_channel);

  struct PaddedPair {
    int key;
    int value;
    char tag;  // Followed by 3 bytes of trailing alignment padding.
  };
  static_assert(sizeof(PaddedPair) == 12);

  PaddedPair raw_items[3];
  // Fill all bytes (including the 3 trailing padding bytes in `raw_items[1]`,
  // the `{0, 0, 0}` sentinel) with non-zero `0xAB` garbage, and put a non-zero
  // element at `raw_items[2]` to verify sentinel detection stops at index 1.
  std::memset(raw_items, 0xAB, sizeof(raw_items));
  raw_items[0].key = 7;
  raw_items[0].value = 42;
  raw_items[0].tag = 'x';
  raw_items[1].key = 0;
  raw_items[1].value = 0;
  raw_items[1].tag = 0;
  raw_items[2].key = 99;
  raw_items[2].value = 99;
  raw_items[2].tag = 'z';

  EXPECT_CALL(mock_channel, CopyFromSandbox(0xEE00, _))
      .WillOnce([&](uintptr_t, absl::Span<char> dst) -> absl::StatusOr<size_t> {
        size_t to_copy = std::min(dst.size(), sizeof(raw_items));
        std::memcpy(dst.data(), raw_items, to_copy);
        return to_copy;
      });

  std::vector<char> buf = ReadNullTerminatedArray<PaddedPair>(sandbox, 0xEE00);
  // Must stop at `raw_items[1]` (2 elements = 24 bytes) despite non-zero
  // padding bytes in `raw_items[1]`.
  ASSERT_EQ(buf.size(), 2 * sizeof(PaddedPair));
  const auto* pairs = reinterpret_cast<const PaddedPair*>(buf.data());
  EXPECT_EQ(pairs[0].key, 7);
  EXPECT_EQ(pairs[0].value, 42);
  EXPECT_EQ(pairs[0].tag, 'x');
  EXPECT_EQ(pairs[1].key, 0);
  EXPECT_EQ(pairs[1].value, 0);
  EXPECT_EQ(pairs[1].tag, 0);

  // A 1-byte struct with unused bit-field padding bits lacks unique object
  // representations (`!std::has_unique_object_representations_v`) and must
  // still use `IsZeroElement` (`__builtin_clear_padding`) rather than the raw
  // `GetCString` (`'\0'`) fast path.
  struct OneByteBitfieldFlags {
    uint8_t active : 1;
  };
  static_assert(sizeof(OneByteBitfieldFlags) == 1);
  static_assert(
      !std::has_unique_object_representations_v<OneByteBitfieldFlags>);

  OneByteBitfieldFlags raw_flags[3];
  std::memset(raw_flags, 0xFF, sizeof(raw_flags));
  raw_flags[0].active = 1;
  raw_flags[1].active = 0;  // Sentinel byte is `0xFE` due to 7 padding bits.
  raw_flags[2].active = 1;

  EXPECT_EQ(NullTerminatedCount(raw_flags), 1u);
  EXPECT_FALSE(internal::AggregateMemberwiseIsZero(raw_flags[0]));
  EXPECT_TRUE(internal::AggregateMemberwiseIsZero(raw_flags[1]));
  EXPECT_CALL(mock_channel, CopyFromSandbox(0xEE80, _))
      .WillOnce([&](uintptr_t, absl::Span<char> dst) -> absl::StatusOr<size_t> {
        size_t to_copy = std::min(dst.size(), sizeof(raw_flags));
        std::memcpy(dst.data(), raw_flags, to_copy);
        return to_copy;
      });
  std::vector<char> flags_buf =
      ReadNullTerminatedArray<OneByteBitfieldFlags>(sandbox, 0xEE80);
  ASSERT_EQ(flags_buf.size(), 2 * sizeof(OneByteBitfieldFlags));
  const auto* flags =
      reinterpret_cast<const OneByteBitfieldFlags*>(flags_buf.data());
  EXPECT_EQ(flags[0].active, 1u);
  EXPECT_EQ(flags[1].active, 0u);
}

TEST(NullTerminatedArrayTest,
     SupportsWidePaddedStructsArrayFieldsAndMemberwiseFallback) {
  MockRPCChannel mock_channel;
  MockSandbox sandbox(&mock_channel);

  // 10-field C struct with internal and trailing alignment padding.
  struct TenFieldCodecOption {
    const char* name;
    char short_flag;  // +3 bytes padding before `id`.
    int id;
    int min_val;
    int max_val;
    int default_val;
    uint64_t flags;
    const char* help;
    int group_id;
    char category;  // +7 bytes trailing padding.
  };

  TenFieldCodecOption wide_items[3];
  std::memset(wide_items, 0xCD, sizeof(wide_items));
  wide_items[0] = {nullptr, 'b', 1, 0, 100, 50, 0x1, nullptr, 2, 'a'};
  // Zero only the declared fields of the sentinel at index 1 while leaving all
  // alignment padding bytes filled with `0xCD`.
  wide_items[1].name = nullptr;
  wide_items[1].short_flag = 0;
  wide_items[1].id = 0;
  wide_items[1].min_val = 0;
  wide_items[1].max_val = 0;
  wide_items[1].default_val = 0;
  wide_items[1].flags = 0;
  wide_items[1].help = nullptr;
  wide_items[1].group_id = 0;
  wide_items[1].category = 0;
  wide_items[2] = {nullptr, 'z', 9, 0, 1, 0, 0, nullptr, 0, 'z'};

  EXPECT_EQ(NullTerminatedCount(wide_items), 1u);
  EXPECT_CALL(mock_channel, CopyFromSandbox(0xEF00, _))
      .WillOnce([&](uintptr_t, absl::Span<char> dst) -> absl::StatusOr<size_t> {
        size_t to_copy = std::min(dst.size(), sizeof(wide_items));
        std::memcpy(dst.data(), wide_items, to_copy);
        return to_copy;
      });
  std::vector<char> wide_buf =
      ReadNullTerminatedArray<TenFieldCodecOption>(sandbox, 0xEF00);
  EXPECT_EQ(wide_buf.size(), 2 * sizeof(TenFieldCodecOption));

  // Struct with an embedded C array member followed by alignment padding:
  // verifies `IsZeroElement` inspects array elements while clearing padding.
  struct ArrayFieldEntry {
    char short_name[5];  // +3 bytes padding before `code`.
    int code;
  };
  ArrayFieldEntry array_items[3];
  std::memset(array_items, 0xEE, sizeof(array_items));
  std::memcpy(array_items[0].short_name, "opus", 5);
  array_items[0].code = 10;
  std::memset(array_items[1].short_name, 0, sizeof(array_items[1].short_name));
  array_items[1].code = 0;
  std::memcpy(array_items[2].short_name, "tail", 5);
  array_items[2].code = 20;

  EXPECT_EQ(NullTerminatedCount(array_items), 1u);
  // Also directly exercise the portable C++20 structured-binding fallback used
  // on toolchains without `__builtin_clear_padding` (e.g. Clang <= 22).
  EXPECT_FALSE(internal::AggregateMemberwiseIsZero(wide_items[0]));
  EXPECT_TRUE(internal::AggregateMemberwiseIsZero(wide_items[1]));
  EXPECT_FALSE(internal::AggregateMemberwiseIsZero(array_items[0]));
  EXPECT_TRUE(internal::AggregateMemberwiseIsZero(array_items[1]));

#if __has_builtin(__builtin_clear_padding)
  // Struct with > 16 fields (19 fields) AND non-zero trailing alignment padding
  // bytes (`0xCD`) in the sentinel element: `__builtin_clear_padding` handles
  // arbitrary field counts without a structured-binding arity limit.
  struct NineteenFieldPaddedEntry {
    int f0, f1, f2, f3, f4, f5, f6, f7, f8;
    int f9, f10, f11, f12, f13, f14, f15, f16, f17;
    char tag;  // +3 bytes trailing padding.
  };
  NineteenFieldPaddedEntry large_items[3];
  std::memset(large_items, 0xCD, sizeof(large_items));
  large_items[0] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'x'};
  large_items[1].f0 = 0;
  large_items[1].f1 = 0;
  large_items[1].f2 = 0;
  large_items[1].f3 = 0;
  large_items[1].f4 = 0;
  large_items[1].f5 = 0;
  large_items[1].f6 = 0;
  large_items[1].f7 = 0;
  large_items[1].f8 = 0;
  large_items[1].f9 = 0;
  large_items[1].f10 = 0;
  large_items[1].f11 = 0;
  large_items[1].f12 = 0;
  large_items[1].f13 = 0;
  large_items[1].f14 = 0;
  large_items[1].f15 = 0;
  large_items[1].f16 = 0;
  large_items[1].f17 = 0;
  large_items[1].tag = 0;
  large_items[2] = {9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'z'};
  EXPECT_EQ(NullTerminatedCount(large_items), 1u);
#endif
}

TEST(NullTerminatedArrayTest, ReadNullTerminatedArrayCheckFailures) {
  MockSandbox sandbox_no_channel(nullptr);
  EXPECT_DEATH(ReadNullTerminatedArray(sandbox_no_channel, 0x1000, 0),
               "ReadNullTerminatedArray called with elem_size 0");
  EXPECT_DEATH(
      ReadNullTerminatedArray(sandbox_no_channel, 0x1000, sizeof(int)),
      "Sandbox RPCChannel is null while reading null-terminated array");

  // Single-byte `GetCString` failure, multi-byte `CopyFromSandbox` failure,
  // short read (`< elem_size`), and 10 MiB DoS cap enforcement.
  EXPECT_DEATH(
      {
        MockRPCChannel ch;
        MockSandbox sb(&ch);
        EXPECT_CALL(ch, Strlen(_))
            .WillOnce(Return(absl::InternalError("bad cstr")));
        ReadNullTerminatedArray(sb, 0x1000, 1);
      },
      "Failed to fetch remote null-terminated byte array");

  EXPECT_DEATH(
      {
        MockRPCChannel ch;
        MockSandbox sb(&ch);
        EXPECT_CALL(ch, CopyFromSandbox(_, _))
            .WillRepeatedly(Return(absl::InternalError("unmapped")));
        ReadNullTerminatedArray(sb, 0x1000, sizeof(uint32_t));
      },
      "Failed to read elements of null-terminated array");

  EXPECT_DEATH(
      {
        MockRPCChannel ch;
        MockSandbox sb(&ch);
        EXPECT_CALL(ch, CopyFromSandbox(_, _)).WillOnce(Return(2u));
        ReadNullTerminatedArray(sb, 0x1000, sizeof(uint32_t));
      },
      "Short read of null-terminated array element");

  EXPECT_DEATH(
      {
        MockRPCChannel ch;
        MockSandbox sb(&ch);
        EXPECT_CALL(ch, CopyFromSandbox(_, _))
            .WillRepeatedly([](uintptr_t, absl::Span<char> dst) {
              std::memset(dst.data(), 0x7F, dst.size());
              return dst.size();
            });
        ReadNullTerminatedArray(sb, 0x1000, sizeof(uint32_t));
      },
      "exceeds maximum size");
}

TEST(LwboxRuntimeTest, CallbackBoolArgIgnoresUpper56GarbageBits) {
  using ArgsTuple =
      std::tuple<int, int, int, int, int, int, int, int, bool, bool>;
  auto fn = [](int, int, int, int, int, int, int, int, bool b0,
               bool b1) -> uint64_t {
    return (b0 ? 1ULL : 0ULL) | (b1 ? 2ULL : 0ULL);
  };
  uint64_t raw_args[20] = {};
  // Overflow stack slots (`[16..19]` on x86_64/AArch64/RISC-V once the integer
  // argument registers are used up) may have arbitrary upper 56 bits from an
  // 8-byte stack load; `CastArg<bool>` must inspect only the low byte.
#if defined(__aarch64__) || defined(__riscv)
  raw_args[16] = 0xdeadbeefcafebabeULL & ~0xffULL;  // low byte == 0 -> false
  raw_args[17] = 0xdeadbeefcafeba01ULL;             // low byte == 1 -> true
#else
  raw_args[18] = 0xdeadbeefcafebabeULL & ~0xffULL;  // low byte == 0 -> false
  raw_args[19] = 0xdeadbeefcafeba01ULL;             // low byte == 1 -> true
#endif
  EXPECT_EQ(sapi::v::callback_internal::CallHelper<ArgsTuple>(
                fn, absl::MakeConstSpan(raw_args)),
            2ULL);
}

TEST(LwboxRuntimeTest,
     CallbackEnumTruncationRemotePtrReturnAndEmptyFunctorGuard) {
  enum CMode : int32_t {
    kModeNeg = -2,
    kModeZero = 0,
    kModePos = 3,
  };
  enum class ByteOp : uint8_t {
    kNop = 0,
    kStep = 5,
  };
  enum class BoolFlag : bool {
    kOff = false,
    kOn = true,
  };

  using EnumArgsTuple = std::tuple<CMode, CMode, ByteOp, BoolFlag>;
  auto cb_fn = [](CMode m0, CMode m1, ByteOp op,
                  BoolFlag flag) -> sapi::v::RemotePtr {
    EXPECT_EQ(m0, kModePos);
    EXPECT_EQ(m1, kModeNeg);
    EXPECT_EQ(op, ByteOp::kStep);
    EXPECT_EQ(flag, BoolFlag::kOn);
    const uintptr_t addr = (m0 == kModePos && m1 == kModeNeg &&
                            op == ByteOp::kStep && flag == BoolFlag::kOn)
                               ? uintptr_t{0x123456789abcULL}
                               : uintptr_t{0};
    return sapi::v::RemotePtr(addr);
  };

  // Populate upper 32/56 bits with garbage to verify narrow enum underlying
  // truncation and `RemotePtr` return marshaling.
  const uint64_t kDirtyEnumArgs[] = {
      0xdeadbeef00000003ULL,  // low 32 bits == 3 (kModePos)
      0xfeedfacefffffffeULL,  // low 32 bits == -2 (kModeNeg)
      0xcafebabe12345605ULL,  // low 8 bits == 5 (ByteOp::kStep)
      0xdeadbeefcafeba01ULL,  // low 8 bits == 1 (BoolFlag::kOn)
  };
  EXPECT_EQ(sapi::v::callback_internal::CallHelper<EnumArgsTuple>(
                cb_fn, absl::MakeConstSpan(kDirtyEnumArgs)),
            0x123456789abcULL);

  // Empty function pointer and moved-out callback functors fail `Allocate`
  // with `FailedPreconditionError("Callback functor is empty")`.
  ScopedSandbox scoped;
  int (*null_fn)(int) = nullptr;
  sapi::v::Callback null_cb(null_fn);
  EXPECT_THAT(scoped.sandbox().Allocate(&null_cb, /*automatic_free=*/false),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("Callback functor is empty")));

  sapi::v::Callback reusable_cb([](int x) { return x + 1; });
  ASSERT_THAT(scoped.sandbox().Allocate(&reusable_cb, /*automatic_free=*/false),
              IsOk());
  ASSERT_THAT(scoped.sandbox().Free(&reusable_cb), IsOk());
  EXPECT_THAT(scoped.sandbox().Allocate(&reusable_cb, /*automatic_free=*/false),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("Callback functor is empty")));
}

}  // namespace
}  // namespace lwbox
}  // namespace sapi
