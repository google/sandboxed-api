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

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/sandbox.h"
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
};

class MockSandbox : public sapi::SandboxBase {
 public:
  explicit MockSandbox(sapi::RPCChannel* channel) : channel_(channel) {}

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
  ScopedSandbox() : sandbox_(&channel_) {}
  ~ScopedSandbox() {
    SandboxeeCallbackRegistry::Instance()->ForgetSandbox(sandbox_);
  }

  MockSandbox& sandbox() { return sandbox_; }

 private:
  MockRPCChannel channel_;
  MockSandbox sandbox_;
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

}  // namespace
}  // namespace lwbox
}  // namespace sapi
