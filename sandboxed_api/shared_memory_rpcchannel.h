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

#ifndef SANDBOXED_API_SHARED_MEMORY_RPCCHANNEL_H_
#define SANDBOXED_API_SHARED_MEMORY_RPCCHANNEL_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <tuple>

#include "absl/base/thread_annotations.h"
#include "absl/container/btree_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/var_type.h"

namespace sapi {

namespace internal {

// This is a simple implementation of the shared memory allocator. It uses a
// std::map to keep track of the blocks of memory that have been allocated, and
// a std::map to keep track of the free blocks.
// This allocator implements a best-fit strategy to avoid fragmentation of the
// memory.
class SimpleAllocator {
  struct Metadata {
    uintptr_t addr;
    bool is_free;
    size_t size;
  };

 public:
  SimpleAllocator() = default;
  SimpleAllocator(void* local_ptr, size_t size);
  absl::StatusOr<void*> Allocate(size_t size);
  absl::StatusOr<void*> Reallocate(void* old_addr, size_t size);
  absl::Status Free(void* ptr);
  absl::StatusOr<const SimpleAllocator::Metadata*> GetAllocationMetadata(
      void* ptr) const;

 private:
  struct MetadataComp {
    using is_transparent = void;
    bool operator()(const Metadata* l, const Metadata* r) const {
      return std::tie(l->size, l->addr) < std::tie(r->size, r->addr);
    }
    bool operator()(const size_t size, const Metadata* r) const {
      return size < r->size;
    }
    bool operator()(const Metadata* l, const size_t size) const {
      return l->size < size;
    }
  };
  void SplitBlock(Metadata* block, size_t size)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  void* local_ptr_ = nullptr;
  size_t size_ = 0;
  mutable absl::Mutex mutex_;

  // This is so that we can amortize the cost of searching a block when invoking
  // Free(), but also to have bidirectional iterators.
  // A `std::map` is needed for pointer stability.
  std::map<uintptr_t, Metadata> all_blocks_ ABSL_GUARDED_BY(mutex_);
  // This will keep the free blocks sorted by size and then by address so that
  // we can always try to fit the smallest block that fits the requested size.
  // This is to avoid fragmentation of the memory as much as possible.
  absl::btree_set<Metadata*, MetadataComp> free_blocks_ ABSL_GUARDED_BY(mutex_);
};

}  // namespace internal

// This class implements RPCChannel with the SAPI sandboxee using shared memory.
// It uses Sandbox2RPCChannel as a fallback if the requested operation is not
// supported by the shared memory or to forward requests unhandled by the shared
// memory mechanism via the comms channel.
class SharedMemoryRPCChannel : public RPCChannel {
 public:
  explicit SharedMemoryRPCChannel(std::unique_ptr<RPCChannel> rpc_channel,
                                  size_t size, void* local_base_address,
                                  void* remote_base_address);

  absl::Status Allocate(size_t size, void** addr,
                        bool disable_shared_memory = false) override;

  absl::Status Reallocate(void* old_addr, size_t size,
                          void** new_addr) override;

  absl::Status Free(void* remote_addr) override;

  absl::StatusOr<size_t> CopyToSandbox(uintptr_t remote_ptr,
                                       absl::Span<const char> data) override;

  absl::StatusOr<size_t> CopyFromSandbox(uintptr_t remote_ptr,
                                         absl::Span<char> data) override;

  absl::StatusOr<size_t> Strlen(void* remote_ptr) override;

  absl::Status Symbol(const char* symname, void** addr) override;

  absl::Status Exit() override;

  absl::Status SendFD(int local_fd, int* remote_fd) override;

  absl::Status RecvFD(int remote_fd, int* local_fd) override;

  absl::Status Close(int remote_fd) override;

  absl::Status Call(const FuncCall& call, uint32_t tag, FuncRet* ret,
                    v::Type exp_type) override;

  ~SharedMemoryRPCChannel() override = default;

 private:
  bool IsWithinRemoteSharedMemoryRegion(uintptr_t remote_ptr);

  bool IsWithinLocalSharedMemoryRegion(uintptr_t local_ptr);

  absl::Status ReallocateInNonSharedMemory(void* local_addr, size_t size,
                                           void** new_addr);

  void* ToLocalAddr(uintptr_t remote_addr);

  void* ToRemoteAddr(uintptr_t local_addr);

  absl::Status EnsureWithinAllocationBounds(void* local_ptr, size_t size);

  std::unique_ptr<RPCChannel> rpcchannel_;
  internal::SimpleAllocator allocator_;
  uintptr_t local_base_address_;
  uintptr_t remote_base_address_;
  size_t size_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_SHARED_MEMORY_RPCCHANNEL_H_
