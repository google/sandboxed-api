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
#include <optional>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/sandbox.h"
#include "sandboxed_api/var_ptr.h"

namespace sapi {
namespace lwbox {

ContextBindingRegistry* ContextBindingRegistry::Instance() {
  static ContextBindingRegistry* instance = new ContextBindingRegistry();
  return instance;
}

std::optional<size_t> ContextBindingRegistry::BindSize(
    const void* absl_nonnull context, absl::string_view name, size_t size) {
  CHECK_NE(context, nullptr)
      << "BindSize called with null context for '" << name << "'";
  absl::MutexLock lock(mutex_);
  auto [it, inserted] =
      sizes_.try_emplace(std::make_pair(context, std::string(name)), size);
  if (!inserted) {
    size_t old_size = it->second;
    it->second = size;
    return old_size;
  }
  return std::nullopt;
}

size_t ContextBindingRegistry::GetSize(const void* absl_nonnull context,
                                       absl::string_view name) {
  CHECK_NE(context, nullptr)
      << "GetSize called with null context for '" << name << "'";
  absl::MutexLock lock(mutex_);
  auto it = sizes_.find({context, std::string(name)});
  CHECK(it != sizes_.end())
      << "Missing context size binding for label: " << name;
  return it->second;
}

void ContextBindingRegistry::RetainPointer(const void* absl_nonnull context,
                                           absl::string_view name,
                                           uintptr_t remote_ptr, size_t bytes) {
  CHECK_NE(context, nullptr)
      << "RetainPointer called with null context for '" << name
      << "', leaking remote_ptr: 0x" << absl::Hex(remote_ptr);
  if (remote_ptr == 0) return;

  absl::MutexLock lock(mutex_);
  auto [it, inserted] =
      retained_.try_emplace(std::make_pair(context, std::string(name)),
                            RetainedBuffer{remote_ptr, bytes});
  if (!inserted) {
    CHECK_EQ(it->second.remote_ptr, remote_ptr)
        << "RetainPointer overwriting previously retained pointer for '" << name
        << "' without prior ClearBindings (old: 0x"
        << absl::Hex(it->second.remote_ptr) << ", new: 0x"
        << absl::Hex(remote_ptr) << ")";
    it->second.bytes = bytes;
  }
}

void ContextBindingRegistry::ClearBindings(sapi::SandboxBase& sandbox,
                                           const void* absl_nullable context) {
  if (context == nullptr) return;

  // Use sets to deduplicate buffers and prevent double-freeing if multiple
  // bindings point to the same host or remote pointer.
  // NOTE: This could be improved/optimized to a stack-allocated flat set
  // (e.g., absl::InlinedVector) to eliminate heap allocations during teardown
  // if we find it necessary.
  absl::flat_hash_set<void*> remote_ptrs_to_free;
  absl::flat_hash_set<void*> host_ptrs_to_free;
  {
    absl::MutexLock lock(mutex_);

    for (auto it = sizes_.begin(); it != sizes_.end();) {
      if (it->first.first == context) {
        sizes_.erase(it++);
      } else {
        ++it;
      }
    }

    for (auto it = retained_.begin(); it != retained_.end();) {
      if (it->first.first == context) {
        void* ptr = reinterpret_cast<void*>(it->second.remote_ptr);
        if (ptr != nullptr) {
          remote_ptrs_to_free.insert(ptr);
        }
        retained_.erase(it++);
      } else {
        ++it;
      }
    }

    for (auto it = bound_host_buffers_.begin();
         it != bound_host_buffers_.end();) {
      if (it->first.first == context) {
        if (it->second.host_ptr != nullptr) {
          host_ptrs_to_free.insert(it->second.host_ptr);
        }
        bound_host_buffers_.erase(it++);
      } else {
        ++it;
      }
    }
  }

  // Free host and remote memory buffers outside mutex_ to avoid holding
  // the process-wide registry lock during IPC round-trips or allocator
  // operations.
  for (void* host_ptr : host_ptrs_to_free) {
    std::free(host_ptr);
  }

  if (!remote_ptrs_to_free.empty()) {
    CHECK_NE(sandbox.rpc_channel(), nullptr)
        << "Sandbox RPCChannel is null while clearing remote bindings";
    for (void* remote_ptr : remote_ptrs_to_free) {
      CHECK_OK(sandbox.rpc_channel()->Free(remote_ptr))
          << "Failed to free retained remote pointer: " << remote_ptr;
    }
  }
}

std::optional<ContextBindingRegistry::BoundHostBuffer>
ContextBindingRegistry::GetBoundHostBuffer(const void* absl_nonnull context,
                                           absl::string_view name) {
  CHECK_NE(context, nullptr)
      << "GetBoundHostBuffer called with null context for '" << name << "'";
  absl::MutexLock lock(mutex_);
  auto it = bound_host_buffers_.find({context, std::string(name)});
  if (it == bound_host_buffers_.end()) return std::nullopt;
  return it->second;
}

void ContextBindingRegistry::BindHostBuffer(const void* absl_nonnull context,
                                            absl::string_view name,
                                            uintptr_t remote_ptr,
                                            void* absl_nonnull host_ptr,
                                            size_t bytes) {
  CHECK_NE(context, nullptr)
      << "BindHostBuffer called with null context for '" << name << "'";
  CHECK_NE(host_ptr, nullptr)
      << "BindHostBuffer called with null host_ptr for '" << name << "'";

  absl::MutexLock lock(mutex_);
  auto [it, inserted] = bound_host_buffers_.try_emplace(
      std::make_pair(context, std::string(name)),
      BoundHostBuffer{remote_ptr, host_ptr, bytes});
  if (!inserted) {
    if (it->second.host_ptr != host_ptr) {
      std::free(it->second.host_ptr);
    }
    it->second = BoundHostBuffer{remote_ptr, host_ptr, bytes};
  }
}

void SyncFromSandboxToHost(sapi::SandboxBase& sandbox, uintptr_t remote_ptr,
                           void* host_ptr, size_t bytes) {
  if (remote_ptr == 0 || host_ptr == nullptr || bytes == 0) {
    return;
  }
  CHECK_NE(sandbox.rpc_channel(), nullptr)
      << "Sandbox RPCChannel is null while syncing buffer from sandbox";
  CHECK_OK(sandbox.rpc_channel()->CopyFromSandbox(
      remote_ptr, absl::MakeSpan(reinterpret_cast<char*>(host_ptr), bytes)))
      << "Failed to copy memory from sandbox at 0x" << absl::Hex(remote_ptr);
}

GlobalStringRegistry* GlobalStringRegistry::Instance() {
  static GlobalStringRegistry* instance = new GlobalStringRegistry();
  return instance;
}

const char* GlobalStringRegistry::GetOrFetch(sapi::SandboxBase& sandbox,
                                             const void* remote_ptr) {
  if (remote_ptr == nullptr) {
    return nullptr;
  }

  {
    absl::MutexLock lock(mutex_);
    auto it = strings_.find(remote_ptr);
    if (it != strings_.end()) {
      return it->second.c_str();
    }
  }

  absl::StatusOr<std::string> remote_str =
      sandbox.GetCString(sapi::v::RemotePtr(remote_ptr));
  CHECK_OK(remote_str.status())
      << "Failed to fetch remote string at " << remote_ptr;

  absl::MutexLock lock(mutex_);
  auto [it, inserted] = strings_.insert({remote_ptr, *std::move(remote_str)});
  return it->second.c_str();
}

}  // namespace lwbox
}  // namespace sapi
