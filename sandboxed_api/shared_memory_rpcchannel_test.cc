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

#include "sandboxed_api/shared_memory_rpcchannel.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/var_type.h"

namespace sapi {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;

TEST(SharedMemoryAllocatorTest, BasicAllocation) {
  std::vector<uint8_t> buffer(1024);
  internal::SimpleAllocator allocator(buffer.data(), buffer.size());
  SAPI_ASSERT_OK_AND_ASSIGN(void* remote_ptr, allocator.Allocate(1 << 4));
  ASSERT_NE(remote_ptr, nullptr);
  SAPI_ASSERT_OK_AND_ASSIGN(auto metadata,
                            allocator.GetAllocationMetadata(remote_ptr));
  EXPECT_EQ(metadata->size, 1 << 4);
}

TEST(SharedMemoryAllocatorTest, NotEnoughMemory) {
  std::vector<uint8_t> buffer(1024);
  internal::SimpleAllocator allocator(buffer.data(), buffer.size());
  ASSERT_THAT(allocator.Allocate(2048),
              StatusIs(absl::StatusCode::kResourceExhausted));
}

TEST(SharedMemoryAllocatorTest, AllocateWholeMemory) {
  std::vector<uint8_t> buffer(1024);
  internal::SimpleAllocator allocator(buffer.data(), buffer.size());
  SAPI_ASSERT_OK_AND_ASSIGN(void* remote_ptr,
                            allocator.Allocate(buffer.size()));
  ASSERT_NE(remote_ptr, nullptr);
}

TEST(SharedMemoryAllocatorTest, AllocateThenNotEnoughMemory) {
  std::vector<uint8_t> buffer(1024);
  internal::SimpleAllocator allocator(buffer.data(), buffer.size());
  SAPI_ASSERT_OK(allocator.Allocate(512));
  ASSERT_THAT(allocator.Allocate(520),
              StatusIs(absl::StatusCode::kResourceExhausted));
}

TEST(SharedMemoryAllocatorTest, FreeBlock) {
  std::vector<uint8_t> buffer(1024);
  internal::SimpleAllocator allocator(buffer.data(), buffer.size());
  SAPI_ASSERT_OK_AND_ASSIGN(void* ptr1, allocator.Allocate(512));
  SAPI_ASSERT_OK_AND_ASSIGN(void* ptr2, allocator.Allocate(256));
  SAPI_ASSERT_OK(allocator.Free(ptr1));
  SAPI_ASSERT_OK(allocator.Free(ptr2));
}

TEST(SharedMemoryAllocatorTest, Reallocate) {
  std::vector<uint8_t> buffer(1024);
  internal::SimpleAllocator allocator(buffer.data(), buffer.size());
  SAPI_ASSERT_OK_AND_ASSIGN(void* ptr1, allocator.Allocate(256));
  SAPI_ASSERT_OK_AND_ASSIGN(void* ptr2, allocator.Allocate(8));
  SAPI_ASSERT_OK_AND_ASSIGN(void* new_ptr, allocator.Reallocate(ptr1, 272));
  ASSERT_NE(ptr1, new_ptr);
  SAPI_ASSERT_OK(allocator.Free(new_ptr));
  SAPI_ASSERT_OK(allocator.Free(ptr2));
}

TEST(SharedMemoryAllocatorTest, InvalidFree) {
  std::vector<uint8_t> buffer(1024);
  internal::SimpleAllocator allocator(buffer.data(), buffer.size());
  SAPI_ASSERT_OK_AND_ASSIGN(void* ptr1, allocator.Allocate(512));
  ASSERT_THAT(allocator.Free(reinterpret_cast<uint8_t*>(ptr1) + 1),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(SharedMemoryAllocatorTest, ReallocateWithMerge) {
  std::vector<uint8_t> buffer(1024);
  internal::SimpleAllocator allocator(buffer.data(), buffer.size());
  SAPI_ASSERT_OK_AND_ASSIGN(void* ptr1,
                            allocator.Allocate((buffer.size() / 2) + 1));
  SAPI_ASSERT_OK_AND_ASSIGN(
      void* ptr2, allocator.Reallocate(ptr1, (buffer.size() / 2) + 2));
  ASSERT_EQ(ptr1, ptr2);
  SAPI_ASSERT_OK(allocator.Free(ptr2));
}

TEST(SharedMemoryAllocatorTest, FreeWillMergeBlocks) {
  std::vector<uint8_t> buffer(1024);
  internal::SimpleAllocator allocator(buffer.data(), buffer.size());
  SAPI_ASSERT_OK_AND_ASSIGN(void* ptr1, allocator.Allocate(buffer.size() / 4));
  SAPI_ASSERT_OK_AND_ASSIGN(void* ptr2, allocator.Allocate(buffer.size() / 4));
  SAPI_ASSERT_OK_AND_ASSIGN(void* ptr3, allocator.Allocate(buffer.size() / 4));
  SAPI_ASSERT_OK(allocator.Free(ptr1));
  SAPI_ASSERT_OK(allocator.Free(ptr3));
  SAPI_ASSERT_OK(allocator.Free(ptr2));
  SAPI_ASSERT_OK_AND_ASSIGN(ptr1, allocator.Allocate(buffer.size()));
}

TEST(SharedMemoryAllocatorTest, MultipleAllocationAndFree) {
  constexpr size_t kNumAllocations = 100000;
  std::vector<void*> ptrs(kNumAllocations, nullptr);
  std::vector<uint8_t> buffer(10 << 20);
  internal::SimpleAllocator allocator(buffer.data(), buffer.size());
  for (size_t i = 0; i < kNumAllocations; ++i) {
    SAPI_ASSERT_OK_AND_ASSIGN(ptrs[i], allocator.Allocate(32));
  }
  for (size_t i = 0; i < kNumAllocations; ++i) {
    SAPI_ASSERT_OK(allocator.Free(ptrs[i]));
  }
}

class MockRPCChannel : public RPCChannel {
 public:
  MOCK_METHOD(absl::Status, Allocate,
              (size_t size, void** addr, bool disable_shared_memory),
              (override));
  MOCK_METHOD(absl::Status, Reallocate,
              (void* old_addr, size_t size, void** new_addr), (override));
  MOCK_METHOD(absl::Status, Free, (void* addr), (override));
  MOCK_METHOD(absl::StatusOr<size_t>, CopyToSandbox,
              (uintptr_t remote_ptr, absl::Span<const char> data), (override));
  MOCK_METHOD(absl::StatusOr<size_t>, CopyFromSandbox,
              (uintptr_t remote_ptr, absl::Span<char> data), (override));
  MOCK_METHOD(absl::StatusOr<size_t>, Strlen, (void* remote_ptr), (override));
  MOCK_METHOD(absl::Status, Symbol, (const char* symname, void** addr),
              (override));
  MOCK_METHOD(absl::Status, Exit, (), (override));
  MOCK_METHOD(absl::Status, SendFD, (int local_fd, int* remote_fd), (override));
  MOCK_METHOD(absl::Status, RecvFD, (int remote_fd, int* local_fd), (override));
  MOCK_METHOD(absl::Status, Close, (int remote_fd), (override));
  MOCK_METHOD(absl::Status, Call,
              (const FuncCall& call, uint32_t tag, FuncRet* ret,
               v::Type exp_type),
              (override));
};

class SharedMemoryRPCChannelTest : public ::testing::Test {
 public:
  static constexpr uintptr_t kRemoteBaseAddress = 0x100000000;

  SharedMemoryRPCChannelTest() = default;

  void SetUp() override {
    auto mock = std::make_unique<testing::StrictMock<MockRPCChannel>>();
    mock_rpc_channel_ = mock.get();
    buffer_ = std::vector<uint8_t>(1 << 20);
    rpc_channel_ = std::make_unique<SharedMemoryRPCChannel>(
        std::move(mock), buffer_.size(), buffer_.data(),
        reinterpret_cast<void*>(kRemoteBaseAddress));
  }

  bool IsWithinSharedMemoryRegion(void* remote_ptr) {
    return reinterpret_cast<uintptr_t>(remote_ptr) >= kRemoteBaseAddress &&
           reinterpret_cast<uintptr_t>(remote_ptr) <
               kRemoteBaseAddress + buffer_.size();
  }

  SharedMemoryRPCChannel* rpc_channel() { return rpc_channel_.get(); }

  size_t SharedMemorySize() { return buffer_.size(); }

  void* ToLocalAddr(void* remote_addr) {
    size_t offset =
        reinterpret_cast<uintptr_t>(remote_addr) - kRemoteBaseAddress;
    if (offset >= buffer_.size()) {
      return nullptr;
    }
    return &buffer_[offset];
  }

  void ExpectAllocationRequest(void* ret) {
    EXPECT_CALL(*mock_rpc_channel_,
                Allocate(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(DoAll(SetArgPointee<1>(ret), Return(absl::OkStatus())));
  }

  void ExpectReallocationRequest(void* ret) {
    EXPECT_CALL(*mock_rpc_channel_,
                Reallocate(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(DoAll(SetArgPointee<2>(ret), Return(absl::OkStatus())));
  }

  void ExpectStrlenRequest(size_t len) {
    EXPECT_CALL(*mock_rpc_channel_, Strlen(::testing::_)).WillOnce(Return(len));
  }

  void ExpectFreeRequest() {
    EXPECT_CALL(*mock_rpc_channel_, Free(::testing::_))
        .WillOnce(Return(absl::OkStatus()));
  }

  ~SharedMemoryRPCChannelTest() {}

  std::unique_ptr<SharedMemoryRPCChannel> rpc_channel_;
  testing::StrictMock<MockRPCChannel>* mock_rpc_channel_;
  std::vector<uint8_t> buffer_;
};

TEST_F(SharedMemoryRPCChannelTest, AllocatesDataOnSharedMemory) {
  void* ptr1;
  SAPI_ASSERT_OK(rpc_channel()->Allocate(512, &ptr1));
  ASSERT_TRUE(IsWithinSharedMemoryRegion(ptr1));
}

TEST_F(SharedMemoryRPCChannelTest, AllocateFallsBackToRPCChannelWhenOOM) {
  void* ptr1;
  SAPI_ASSERT_OK(rpc_channel()->Allocate(SharedMemorySize(), &ptr1));
  ASSERT_TRUE(IsWithinSharedMemoryRegion(ptr1));
  ExpectAllocationRequest(reinterpret_cast<void*>(0x12345678));
  SAPI_ASSERT_OK(rpc_channel()->Allocate(128, &ptr1));
  ASSERT_FALSE(IsWithinSharedMemoryRegion(ptr1));
}

TEST_F(SharedMemoryRPCChannelTest, PreventSharedMemoryAllocation) {
  ExpectAllocationRequest(reinterpret_cast<void*>(0x12345678));
  void* ptr1;
  SAPI_ASSERT_OK(
      rpc_channel()->Allocate(512, &ptr1, /*disable_shared_memory=*/true));
  ASSERT_EQ(reinterpret_cast<uintptr_t>(ptr1), 0x12345678);
  ASSERT_FALSE(IsWithinSharedMemoryRegion(ptr1));
}

TEST_F(SharedMemoryRPCChannelTest, Reallocate) {
  void* ptr1;
  SAPI_ASSERT_OK(rpc_channel()->Allocate(512, &ptr1));
  ASSERT_TRUE(IsWithinSharedMemoryRegion(ptr1));
  void* ptr2;
  SAPI_ASSERT_OK(rpc_channel()->Reallocate(ptr1, 1024, &ptr2));
  ASSERT_TRUE(IsWithinSharedMemoryRegion(ptr2));
}

TEST_F(SharedMemoryRPCChannelTest, ReallocateWithInvalidPointer) {
  void* ptr1;
  SAPI_ASSERT_OK(rpc_channel()->Allocate(512, &ptr1));
  ASSERT_TRUE(IsWithinSharedMemoryRegion(ptr1));
  void* ptr2;
  ASSERT_THAT(rpc_channel()->Reallocate(reinterpret_cast<uint8_t*>(ptr1) + 24,
                                        SharedMemorySize() + 1024, &ptr2),
              StatusIs(absl::StatusCode::kInvalidArgument));
  SAPI_ASSERT_OK(rpc_channel()->Free(ptr1));
}

TEST_F(SharedMemoryRPCChannelTest, ReallocateGoesOOM) {
  void* ptr1;
  SAPI_ASSERT_OK(rpc_channel()->Allocate(SharedMemorySize(), &ptr1));
  ASSERT_TRUE(IsWithinSharedMemoryRegion(ptr1));
  std::vector<uint8_t> buffer(SharedMemorySize() + 1024);
  ExpectAllocationRequest(reinterpret_cast<void*>(0x12345678));
  EXPECT_CALL(*mock_rpc_channel_, CopyToSandbox(0x12345678, ::testing::_))
      .WillOnce(Return(SharedMemorySize()));
  void* ptr2;
  SAPI_ASSERT_OK(
      rpc_channel()->Reallocate(ptr1, SharedMemorySize() + 1024, &ptr2));
  EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr2), 0x12345678);
}

TEST_F(SharedMemoryRPCChannelTest, Free) {
  void* ptr1;
  SAPI_ASSERT_OK(rpc_channel()->Allocate(512, &ptr1));
  ASSERT_TRUE(IsWithinSharedMemoryRegion(ptr1));
  SAPI_ASSERT_OK(rpc_channel()->Free(ptr1));
  ASSERT_THAT(rpc_channel()->Free(ptr1),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(SharedMemoryRPCChannelTest, Strlen) {
  constexpr std::string_view kMessage = "Hello World";
  void* remote_ptr;
  SAPI_ASSERT_OK(rpc_channel()->Allocate(128, &remote_ptr));
  void* ptr1 = ToLocalAddr(remote_ptr);
  ASSERT_NE(ptr1, nullptr);
  memcpy(ptr1, kMessage.data(), kMessage.size());
  reinterpret_cast<char*>(ptr1)[kMessage.size()] = '\0';
  SAPI_ASSERT_OK_AND_ASSIGN(size_t len, rpc_channel()->Strlen(remote_ptr));
  EXPECT_EQ(len, kMessage.size());
  SAPI_ASSERT_OK(rpc_channel()->Free(remote_ptr));
}

TEST_F(SharedMemoryRPCChannelTest, StrLenWithMissingNullTerminator) {
  void* remote_ptr;
  SAPI_ASSERT_OK(rpc_channel()->Allocate(SharedMemorySize(), &remote_ptr));
  std::vector<char> buffer(SharedMemorySize(), 'a');
  SAPI_ASSERT_OK(rpc_channel()->CopyToSandbox(
      reinterpret_cast<uintptr_t>(remote_ptr), absl::MakeSpan(buffer)));
  ASSERT_THAT(rpc_channel()->Strlen(remote_ptr),
              StatusIs(absl::StatusCode::kInternal));
  SAPI_ASSERT_OK(rpc_channel()->Free(remote_ptr));
}

TEST_F(SharedMemoryRPCChannelTest, CopyToSandbox) {
  constexpr std::string_view kMessage = "Hello World";
  void* remote_ptr;
  SAPI_ASSERT_OK(rpc_channel()->Allocate(128, &remote_ptr));
  void* ptr1 = ToLocalAddr(remote_ptr);
  ASSERT_NE(ptr1, nullptr);
  ASSERT_THAT(rpc_channel()->CopyToSandbox(
                  reinterpret_cast<uintptr_t>(remote_ptr), kMessage),
              IsOkAndHolds(kMessage.size()));
  absl::string_view buffer_view(reinterpret_cast<char*>(ptr1), kMessage.size());
  EXPECT_EQ(buffer_view, kMessage);
  SAPI_ASSERT_OK(rpc_channel()->Free(remote_ptr));
}

TEST_F(SharedMemoryRPCChannelTest, CopyFromSandbox) {
  constexpr std::string_view kMessage = "Hello World";
  void* remote_ptr;
  SAPI_ASSERT_OK(rpc_channel()->Allocate(128, &remote_ptr));
  void* ptr1 = ToLocalAddr(remote_ptr);
  ASSERT_NE(ptr1, nullptr);
  memcpy(ptr1, kMessage.data(), kMessage.size());
  std::string buffer_out(kMessage.size(), '\0');
  SAPI_ASSERT_OK_AND_ASSIGN(
      size_t len,
      rpc_channel()->CopyFromSandbox(reinterpret_cast<uintptr_t>(remote_ptr),
                                     absl::MakeSpan(buffer_out)));
  EXPECT_EQ(memcmp(buffer_out.data(), kMessage.data(), len), 0);
  EXPECT_EQ(len, kMessage.size());
  SAPI_ASSERT_OK(rpc_channel()->Free(remote_ptr));
}

TEST_F(SharedMemoryRPCChannelTest, ReallocateOnNonSharedMemory) {
  void* ptr1;
  ExpectAllocationRequest(reinterpret_cast<void*>(0x12345678));
  SAPI_ASSERT_OK(rpc_channel()->Allocate(128, &ptr1,
                                         /*disable_shared_memory=*/true));
  ASSERT_FALSE(IsWithinSharedMemoryRegion(ptr1));
  ASSERT_EQ(reinterpret_cast<uintptr_t>(ptr1), 0x12345678);
  ExpectReallocationRequest(ptr1);
  SAPI_ASSERT_OK(rpc_channel()->Reallocate(ptr1, 128, &ptr1));
  ExpectFreeRequest();
  SAPI_ASSERT_OK(rpc_channel()->Free(ptr1));
}

TEST_F(SharedMemoryRPCChannelTest, CopyToSandboxOnNonSharedMemory) {
  constexpr std::string_view kMessage = "Hello World";
  std::vector<uint8_t> buffer(128);
  ExpectAllocationRequest(buffer.data());
  void* remote_ptr;
  SAPI_ASSERT_OK(rpc_channel()->Allocate(128, &remote_ptr,
                                         /*disable_shared_memory=*/true));
  EXPECT_CALL(*mock_rpc_channel_, CopyToSandbox(::testing::_, ::testing::_))
      .WillOnce(Return(kMessage.size()));
  ASSERT_THAT(rpc_channel()->CopyToSandbox(
                  reinterpret_cast<uintptr_t>(remote_ptr), kMessage),
              IsOkAndHolds(kMessage.size()));
}

TEST_F(SharedMemoryRPCChannelTest, CopyFromSandboxOnNonSharedMemory) {
  std::string buffer_in = "Hello World!";
  ExpectAllocationRequest(buffer_in.data());
  void* remote_ptr;
  SAPI_ASSERT_OK(rpc_channel()->Allocate(buffer_in.size(), &remote_ptr,
                                         /*disable_shared_memory=*/true));
  std::string buffer_out(buffer_in.size(), '\0');
  EXPECT_CALL(*mock_rpc_channel_, CopyFromSandbox(::testing::_, ::testing::_))
      .WillOnce(Return(buffer_in.size()));
  SAPI_ASSERT_OK_AND_ASSIGN(
      size_t len,
      rpc_channel()->CopyFromSandbox(reinterpret_cast<uintptr_t>(remote_ptr),
                                     absl::MakeSpan(buffer_out)));
  EXPECT_EQ(len, buffer_in.size());
}

TEST_F(SharedMemoryRPCChannelTest, StrLenOnNonSharedMemory) {
  std::string buffer_in = "Hello World!";
  ExpectAllocationRequest(buffer_in.data());
  void* remote_ptr;
  SAPI_ASSERT_OK(rpc_channel()->Allocate(buffer_in.size(), &remote_ptr,
                                         /*disable_shared_memory=*/true));
  ExpectStrlenRequest(buffer_in.size());
  SAPI_ASSERT_OK_AND_ASSIGN(size_t len, rpc_channel()->Strlen(remote_ptr));
  EXPECT_EQ(len, buffer_in.size());
}

TEST_F(SharedMemoryRPCChannelTest, FreeOnNonSharedMemory) {
  ExpectAllocationRequest(reinterpret_cast<void*>(0x12345678));
  void* remote_ptr;
  SAPI_ASSERT_OK(rpc_channel()->Allocate(128, &remote_ptr,
                                         /*disable_shared_memory=*/true));
  ASSERT_EQ(reinterpret_cast<uintptr_t>(remote_ptr), 0x12345678);
  ExpectFreeRequest();
  SAPI_ASSERT_OK(rpc_channel()->Free(remote_ptr));
}

TEST_F(SharedMemoryRPCChannelTest, CopyToSandboxOutsideAllocationBoundaries) {
  void* remote_ptr;
  SAPI_ASSERT_OK(rpc_channel()->Allocate(128, &remote_ptr));
  ASSERT_THAT(rpc_channel()->CopyToSandbox(
                  reinterpret_cast<uintptr_t>(remote_ptr) + 120,
                  std::string_view("Hello World!")),
              StatusIs(absl::StatusCode::kInternal));
  SAPI_ASSERT_OK(rpc_channel()->Free(remote_ptr));
}

TEST_F(SharedMemoryRPCChannelTest, CopyFromSandboxOutsideAllocationBoundaries) {
  void* remote_ptr;
  SAPI_ASSERT_OK(rpc_channel()->Allocate(128, &remote_ptr));
  std::string buffer_out(10, '\0');
  ASSERT_THAT(rpc_channel()->CopyFromSandbox(
                  reinterpret_cast<uintptr_t>(remote_ptr) + 120,
                  absl::MakeSpan(buffer_out)),
              StatusIs(absl::StatusCode::kInternal));
  SAPI_ASSERT_OK(rpc_channel()->Free(remote_ptr));
}

TEST_F(SharedMemoryRPCChannelTest, CopyToSandboxInMiddleOfAllocation) {
  constexpr std::string_view kMessage = "Hello World";
  void* remote_ptr;
  SAPI_ASSERT_OK(rpc_channel()->Allocate(128, &remote_ptr));
  ASSERT_THAT(
      rpc_channel()->CopyToSandbox(reinterpret_cast<uintptr_t>(remote_ptr) + 1,
                                   absl::MakeSpan(kMessage)),
      IsOkAndHolds(kMessage.size()));
  void* ptr1 = ToLocalAddr(remote_ptr);
  ASSERT_NE(ptr1, nullptr);
  EXPECT_EQ(memcmp(reinterpret_cast<char*>(ptr1) + 1, kMessage.data(),
                   kMessage.size()),
            0);
  SAPI_ASSERT_OK(rpc_channel()->Free(remote_ptr));
}

TEST_F(SharedMemoryRPCChannelTest, CopyFromSandboxInMiddleOfAllocation) {
  void* remote_ptr;
  SAPI_ASSERT_OK(rpc_channel()->Allocate(128, &remote_ptr));
  std::string buffer_out(10, '\0');
  ASSERT_THAT(rpc_channel()->CopyFromSandbox(
                  reinterpret_cast<uintptr_t>(remote_ptr) + 110,
                  absl::MakeSpan(buffer_out)),
              IsOkAndHolds(buffer_out.size()));
  SAPI_ASSERT_OK(rpc_channel()->Free(remote_ptr));
}

void BM_SharedMemoryAllocateThenFree(benchmark::State& state) {
  std::vector<uint8_t> buffer(1 << 20);
  internal::SimpleAllocator allocator(buffer.data(), buffer.size());
  std::vector<void*> ptrs;
  ptrs.reserve(state.range(0));
  for (auto _ : state) {
    for (int i = 0; i < state.range(0); ++i) {
      SAPI_ASSERT_OK_AND_ASSIGN(void* ptr, allocator.Allocate(32));
      ptrs.push_back(ptr);
    }
    while (!ptrs.empty()) {
      SAPI_ASSERT_OK(allocator.Free(ptrs.back()));
      ptrs.pop_back();
    }
  }
}

BENCHMARK(BM_SharedMemoryAllocateThenFree)->Range(1, 100);

void BM_SharedMemoryReallocate(benchmark::State& state) {
  std::vector<uint8_t> buffer(1 << 20);
  internal::SimpleAllocator allocator(buffer.data(), buffer.size());
  void* ptr = nullptr;
  for (auto _ : state) {
    state.PauseTiming();
    if (ptr != nullptr) {
      SAPI_ASSERT_OK(allocator.Free(ptr));
    }
    SAPI_ASSERT_OK_AND_ASSIGN(ptr, allocator.Allocate(32));
    state.ResumeTiming();
    SAPI_ASSERT_OK_AND_ASSIGN(ptr, allocator.Reallocate(ptr, 64));
  }
}

BENCHMARK(BM_SharedMemoryReallocate);

}  // namespace
}  // namespace sapi
