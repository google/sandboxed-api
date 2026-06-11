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

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <utility>

#include "absl/container/btree_set.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/util/status_macros.h"
#include "sandboxed_api/var_type.h"

namespace sapi {
namespace internal {

namespace {
static constexpr size_t kAlignment = 8;
constexpr size_t RoundUp(size_t size, size_t alignment) {
  return (size + alignment - 1) & ~(alignment - 1);
}

absl::StatusOr<size_t> AlignSize(size_t size) {
  static constexpr size_t kMaxAllocatableSize =
      (std::numeric_limits<size_t>::max() - kAlignment) & ~(kAlignment - 1);

  if (size == 0 || size > kMaxAllocatableSize) {
    return absl::InvalidArgumentError("Size is zero or too large");
  }
  return RoundUp(size, kAlignment);
}
}  // namespace

SimpleAllocator::SimpleAllocator(void* local_ptr, size_t size)
    : local_ptr_(local_ptr), size_(size) {
  CHECK(reinterpret_cast<uintptr_t>(local_ptr) % kAlignment == 0);
  uintptr_t addr = reinterpret_cast<uintptr_t>(local_ptr_);
  auto it = all_blocks_.insert({addr, {addr, true, size_}});
  free_blocks_.emplace(&it.first->second);
}

void SimpleAllocator::SplitBlock(SimpleAllocator::Metadata* block,
                                 size_t size) {
  // We need to split the block into two blocks, one with the size we need
  // and one with the remaining size.
  uintptr_t next_addr = block->addr + size;
  size_t next_size = block->size - size;

  // Update the current block's size.
  block->size = size;

  // Keep track of the new block, which is now free.
  auto ait = all_blocks_.insert({next_addr, {next_addr, true, next_size}});
  free_blocks_.emplace(&ait.first->second);
}

absl::StatusOr<void*> SimpleAllocator::Allocate(size_t size) {
  SAPI_ASSIGN_OR_RETURN(size, AlignSize(size));

  absl::MutexLock lock(mutex_);

  auto it = free_blocks_.lower_bound(size);
  if (it == std::end(free_blocks_)) {
    return absl::ResourceExhaustedError("Not enough memory");
  }

  SimpleAllocator::Metadata* block = *it;
  // We found a block that is large enough. Let's remove it from the free
  // blocks set and set it to `free`.
  free_blocks_.erase(std::move(it));
  block->is_free = false;

  // If the block is larger than the requested size, split it.
  if (block->size > size) {
    SplitBlock(block, size);
  }

  return reinterpret_cast<void*>(block->addr);
}

absl::StatusOr<void*> SimpleAllocator::Reallocate(void* old_addr, size_t size) {
  size_t old_size;
  SAPI_ASSIGN_OR_RETURN(size, AlignSize(size));
  {
    absl::MutexLock lock(mutex_);

    auto it = all_blocks_.find(reinterpret_cast<uintptr_t>(old_addr));
    if (it == all_blocks_.end()) {
      return absl::InvalidArgumentError("Invalid pointer");
    }
    old_size = it->second.size;

    // Let's try to either check whether the size fits in the current block or
    // whether we can merge the current block with the following one.
    SimpleAllocator::Metadata* current_block = &it->second;
    if (current_block->size < size) {
      // We need to try merging the current block with the following one.
      auto next = std::next(it);
      if (next != all_blocks_.end() && next->second.is_free &&
          current_block->size + next->second.size >= size) {
        // We can merge the current block with the following one.
        current_block->size += next->second.size;
        free_blocks_.erase(&next->second);
        // We got iterator stability here, so `it` remains valid after the
        // following erase() call.
        all_blocks_.erase(next);
      }
    }

    if (current_block->size >= size) {
      // If the block is larger than the requested size, split it.
      if (current_block->size > size) {
        SplitBlock(current_block, size);
      }
      return old_addr;
    }
  }
  void* new_addr;
  SAPI_ASSIGN_OR_RETURN(new_addr, Allocate(size));
  memcpy(new_addr, old_addr, std::min(old_size, size));
  SAPI_RETURN_IF_ERROR(Free(old_addr));
  return new_addr;
}

absl::Status SimpleAllocator::Free(void* ptr) {
  absl::MutexLock lock(mutex_);

  auto it = all_blocks_.find(reinterpret_cast<uintptr_t>(ptr));
  if (it == all_blocks_.end() || it->second.is_free) {
    return absl::InvalidArgumentError("Invalid pointer");
  }

  // Let's mark the block as free. We don't insert it directly into the
  // free_blocks_ set, because we first want to check whether we can merge the
  // block with the previous and/or following one.
  it->second.is_free = true;

  auto next = std::next(it);
  if (next != all_blocks_.end() && next->second.is_free) {
    // The next block is free, so we can merge it with the current one being
    // freed. We reuse the current block instead of the the next one, because
    // otherwise it would require rehashing the next one since its base address
    // would change.
    it->second.size += next->second.size;
    free_blocks_.erase(&next->second);
    // We got iterator stability here, so `it` remains valid after the following
    // erase() call.
    all_blocks_.erase(next);
  }

  // Check if we can merge with the previous block.
  if (it != all_blocks_.begin()) {
    auto prev = std::prev(it);
    if (prev->second.is_free) {
      // The previous block is also free, so we can merge it with the current
      // one being freed. We'll use the previous block for same the reason as
      // the forward merge above, to avoid rehashing the prev block.
      // However, we need to erase and reinsert the previous block from the
      // `free_blocks_` set, since its size is now changed and we're comparing
      // elements by size and then by address.
      CHECK_EQ(free_blocks_.erase(&prev->second), 1);
      prev->second.size += it->second.size;
      free_blocks_.insert(&prev->second);
      all_blocks_.erase(it);
      return absl::OkStatus();
    }
  }

  // If we couldn't merge with the previous block, add the current block to
  // the free blocks set.
  free_blocks_.insert(&it->second);
  return absl::OkStatus();
}

absl::StatusOr<const SimpleAllocator::Metadata*>
SimpleAllocator::GetAllocationMetadata(void* ptr) const {
  absl::MutexLock lock(mutex_);

  uintptr_t ptr_addr = reinterpret_cast<uintptr_t>(ptr);
  auto it = all_blocks_.upper_bound(reinterpret_cast<uintptr_t>(ptr));
  if (it == all_blocks_.begin()) {
    return absl::InvalidArgumentError("Invalid pointer");
  }
  --it;
  if (ptr_addr < it->second.addr ||
      ptr_addr >= it->second.addr + it->second.size) {
    return absl::InvalidArgumentError("Invalid pointer");
  }
  return &it->second;
}

}  // namespace internal

SharedMemoryRPCChannel::SharedMemoryRPCChannel(
    std::unique_ptr<RPCChannel> rpc_channel, size_t size,
    void* local_base_address, void* remote_base_address)
    : rpcchannel_(std::move(rpc_channel)),
      allocator_(local_base_address, size),
      local_base_address_(reinterpret_cast<uintptr_t>(local_base_address)),
      remote_base_address_(reinterpret_cast<uintptr_t>(remote_base_address)),
      size_(size) {}

bool SharedMemoryRPCChannel::IsWithinRemoteSharedMemoryRegion(
    const uintptr_t remote_ptr) {
  return remote_ptr >= remote_base_address_ &&
         remote_ptr < remote_base_address_ + size_;
}

bool SharedMemoryRPCChannel::IsWithinLocalSharedMemoryRegion(
    const uintptr_t local_ptr) {
  return local_ptr >= local_base_address_ &&
         local_ptr < local_base_address_ + size_;
}

absl::Status SharedMemoryRPCChannel::Allocate(size_t size, void** addr,
                                              bool disable_shared_memory) {
  if (disable_shared_memory) {
    return rpcchannel_->Allocate(size, addr);
  }

  auto res = allocator_.Allocate(size);
  if (!res.ok()) {
    return rpcchannel_->Allocate(size, addr);
  }

  *addr = ToRemoteAddr(reinterpret_cast<uintptr_t>(*res));
  return absl::OkStatus();
}

absl::Status SharedMemoryRPCChannel::Reallocate(void* old_addr, size_t size,
                                                void** new_addr) {
  uintptr_t remote_addr = reinterpret_cast<uintptr_t>(old_addr);
  if (!IsWithinRemoteSharedMemoryRegion(remote_addr)) {
    return rpcchannel_->Reallocate(old_addr, size, new_addr);
  }

  void* old_local_addr = ToLocalAddr(remote_addr);
  auto res = allocator_.Reallocate(old_local_addr, size);
  if (!res.ok()) {
    // We know that we are in the remote shared memory region, so an invalid
    // pointer here means that the pointer does not point to the beginning of
    // an allocation.
    if (res.status().code() == absl::StatusCode::kInvalidArgument) {
      return res.status();
    }

    return ReallocateInNonSharedMemory(old_local_addr, size, new_addr);
  }
  *new_addr = ToRemoteAddr(reinterpret_cast<uintptr_t>(*res));
  return absl::OkStatus();
}

absl::Status SharedMemoryRPCChannel::Free(void* remote_addr) {
  if (!IsWithinRemoteSharedMemoryRegion(
          reinterpret_cast<uintptr_t>(remote_addr))) {
    return rpcchannel_->Free(remote_addr);
  }
  void* local_addr = ToLocalAddr(reinterpret_cast<uintptr_t>(remote_addr));
  return allocator_.Free(local_addr);
}

absl::StatusOr<size_t> SharedMemoryRPCChannel::CopyToSandbox(
    uintptr_t remote_ptr, absl::Span<const char> data) {
  if (!IsWithinRemoteSharedMemoryRegion(remote_ptr)) {
    return rpcchannel_->CopyToSandbox(remote_ptr, data);
  }
  // The remote_ptr must be in the shared memory region.
  void* local_addr = ToLocalAddr(remote_ptr);
  SAPI_RETURN_IF_ERROR(EnsureWithinAllocationBounds(local_addr, data.size()));
  memcpy(local_addr, data.data(), data.size());
  return data.size();
}

absl::StatusOr<size_t> SharedMemoryRPCChannel::CopyFromSandbox(
    uintptr_t remote_ptr, absl::Span<char> data) {
  if (!IsWithinRemoteSharedMemoryRegion(remote_ptr)) {
    return rpcchannel_->CopyFromSandbox(remote_ptr, data);
  }
  // The remote_ptr must be in the shared memory region.
  void* local_addr = ToLocalAddr(remote_ptr);
  SAPI_RETURN_IF_ERROR(EnsureWithinAllocationBounds(local_addr, data.size()));
  memcpy(data.data(), local_addr, data.size());
  return data.size();
}

absl::StatusOr<size_t> SharedMemoryRPCChannel::Strlen(void* remote_ptr) {
  if (!IsWithinRemoteSharedMemoryRegion(
          reinterpret_cast<uintptr_t>(remote_ptr))) {
    return rpcchannel_->Strlen(remote_ptr);
  }
  // The remote_ptr must be in the shared memory region.
  void* local_addr = ToLocalAddr(reinterpret_cast<uintptr_t>(remote_ptr));

  // We must check that the string is within the bounds of the shared memory
  // region.
  size_t offset = reinterpret_cast<uintptr_t>(local_addr) -
                  reinterpret_cast<uintptr_t>(local_base_address_);
  size_t max_size = size_ - offset;
  size_t len = strnlen(reinterpret_cast<const char*>(local_addr), max_size);
  if (len == max_size) {
    return absl::InternalError("Missing null terminator");
  }
  return len;
}

absl::Status SharedMemoryRPCChannel::Symbol(const char* symname, void** addr) {
  return rpcchannel_->Symbol(symname, addr);
}

absl::Status SharedMemoryRPCChannel::Exit() { return rpcchannel_->Exit(); }

absl::Status SharedMemoryRPCChannel::SendFD(int local_fd, int* remote_fd) {
  return rpcchannel_->SendFD(local_fd, remote_fd);
}

absl::Status SharedMemoryRPCChannel::RecvFD(int remote_fd, int* local_fd) {
  return rpcchannel_->RecvFD(remote_fd, local_fd);
}

absl::Status SharedMemoryRPCChannel::Close(int remote_fd) {
  return rpcchannel_->Close(remote_fd);
}

absl::Status SharedMemoryRPCChannel::Call(const FuncCall& call, uint32_t tag,
                                          FuncRet* ret, v::Type exp_type) {
  return rpcchannel_->Call(call, tag, ret, exp_type);
}

absl::Status SharedMemoryRPCChannel::ReallocateInNonSharedMemory(
    void* local_addr, size_t size, void** new_addr) {
  SAPI_ASSIGN_OR_RETURN(auto old_metadata,
                        allocator_.GetAllocationMetadata(local_addr));
  SAPI_RETURN_IF_ERROR(rpcchannel_->Allocate(size, new_addr));
  if (auto ret = rpcchannel_->CopyToSandbox(
          reinterpret_cast<uintptr_t>(*new_addr),
          absl::MakeSpan(reinterpret_cast<char*>(old_metadata->addr),
                         old_metadata->size));
      !ret.ok()) {
    // Copying data failed, so we just want to free the allocation on the
    // remote side so that the operation doesn't have any side-effects.
    SAPI_RETURN_IF_ERROR(rpcchannel_->Free(*new_addr));
    return ret.status();
  }
  return allocator_.Free(local_addr);
}

void* SharedMemoryRPCChannel::ToLocalAddr(uintptr_t remote_addr) {
  DCHECK(IsWithinRemoteSharedMemoryRegion(remote_addr));
  size_t offset = remote_addr - remote_base_address_;
  return reinterpret_cast<void*>(local_base_address_ + offset);
}

void* SharedMemoryRPCChannel::ToRemoteAddr(uintptr_t local_addr) {
  DCHECK(IsWithinLocalSharedMemoryRegion(local_addr));
  size_t offset = local_addr - local_base_address_;
  return reinterpret_cast<void*>(remote_base_address_ + offset);
}

// Technically, we could only check if the pointer is within the remote shared
// memory region, but this would not allow us to catch cases where the pointer
// is in the shared memory region, but not within the bounds of the allocation.
// This is a stricter check, but it allows us to fail early, so that the
// sandboxee has a chance to report the issue in case there is a bug in the
// implementation.
absl::Status SharedMemoryRPCChannel::EnsureWithinAllocationBounds(
    void* local_ptr, size_t size) {
  SAPI_ASSIGN_OR_RETURN(auto metadata,
                        allocator_.GetAllocationMetadata(local_ptr));
  size_t offset = reinterpret_cast<uintptr_t>(local_ptr) -
                  reinterpret_cast<uintptr_t>(metadata->addr);
  if (offset + size > metadata->size) {
    return absl::InternalError("Range is out of bounds.");
  }
  return absl::OkStatus();
}

}  // namespace sapi
