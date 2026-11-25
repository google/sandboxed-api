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
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
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
  CHECK_LE(bytes, std::numeric_limits<uintptr_t>::max() - remote_ptr)
      << "Remote buffer range overflows uintptr_t at 0x"
      << absl::Hex(remote_ptr) << " + " << bytes;
  CHECK_NE(sandbox.rpc_channel(), nullptr)
      << "Sandbox RPCChannel is null while syncing buffer from sandbox";
  CHECK_OK(sandbox.rpc_channel()->CopyFromSandbox(
      remote_ptr, absl::MakeSpan(reinterpret_cast<char*>(host_ptr), bytes)))
      << "Failed to copy memory from sandbox at 0x" << absl::Hex(remote_ptr);
}

std::vector<char> ReadNullTerminatedArray(
    sapi::SandboxBase& sandbox, uintptr_t remote_ptr, size_t elem_size,
    bool (*absl_nullable is_zero_elem)(const void* elem)) {
  if (remote_ptr == 0) {
    return {};
  }
  CHECK_GT(elem_size, 0u) << "ReadNullTerminatedArray called with elem_size 0";
  if (elem_size == 1 && is_zero_elem == nullptr) {
    CHECK_LE(size_t{1}, std::numeric_limits<uintptr_t>::max() - remote_ptr)
        << "Remote null-terminated scan overflows uintptr_t at 0x"
        << absl::Hex(remote_ptr);
    absl::StatusOr<std::string> str = sandbox.GetCString(
        sapi::v::RemotePtr(reinterpret_cast<const void*>(remote_ptr)));
    CHECK_OK(str.status())
        << "Failed to fetch remote null-terminated byte array at 0x"
        << absl::Hex(remote_ptr);
    std::vector<char> result(str->size() + 1);
    std::memcpy(result.data(), str->data(), str->size() + 1);
    return result;
  }

  CHECK_NE(sandbox.rpc_channel(), nullptr)
      << "Sandbox RPCChannel is null while reading null-terminated array";
  // Cap host staging allocation at 10 MiB (matching `SandboxBase::GetCString`'s
  // default `size` limit) so an unterminated buffer in a compromised sandboxee
  // cannot exhaust host memory.
  constexpr size_t kMaxNullTerminatedBytes = 10ULL * 1024 * 1024;
  // Read in 4 KiB page-bounded batches rather than one `CopyFromSandbox`
  // (`process_vm_readv` syscall) per element. Because MMU page permissions are
  // uniform within any 4 KiB page, reading up to the end of `cur_ptr`'s 4 KiB
  // page (`bytes_to_page_end`) is guaranteed not to fault on an unmapped guard
  // page past the sentinel unless a single element straddles the page boundary,
  // in which case `batch_elems == 1` reads only that element.
  constexpr size_t kPageSize = 4096;
  std::vector<char> result;
  std::vector<char> batch_buf;
  uintptr_t cur_ptr = remote_ptr;
  bool single_element_mode = false;
  while (true) {
    size_t bytes_to_page_end = kPageSize - (cur_ptr & (kPageSize - 1));
    size_t batch_elems =
        single_element_mode
            ? 1
            : std::max<size_t>(1, bytes_to_page_end / elem_size);
    size_t batch_bytes = CheckedMultiply(batch_elems, elem_size);
    CHECK_LE(batch_bytes, std::numeric_limits<uintptr_t>::max() - cur_ptr)
        << "Remote null-terminated scan overflows uintptr_t at 0x"
        << absl::Hex(cur_ptr) << " + " << batch_bytes;
    batch_buf.resize(batch_bytes);

    absl::StatusOr<size_t> bytes_read = sandbox.rpc_channel()->CopyFromSandbox(
        cur_ptr, absl::MakeSpan(batch_buf.data(), batch_bytes));
    if (!bytes_read.ok() && batch_elems > 1) {
      // If `cur_ptr` resides in `SharedMemoryRPCChannel`, `CopyFromSandbox` is
      // a userspace `memcpy` that enforces exact sub-page allocation bounds via
      // `EnsureWithinAllocationBounds`. Fall back to single-element reads so we
      // never read past the allocation's end.
      single_element_mode = true;
      batch_elems = 1;
      batch_bytes = elem_size;
      CHECK_LE(batch_bytes, std::numeric_limits<uintptr_t>::max() - cur_ptr)
          << "Remote null-terminated scan overflows uintptr_t at 0x"
          << absl::Hex(cur_ptr) << " + " << batch_bytes;
      batch_buf.resize(batch_bytes);
      bytes_read = sandbox.rpc_channel()->CopyFromSandbox(
          cur_ptr, absl::MakeSpan(batch_buf.data(), batch_bytes));
    }
    CHECK_OK(bytes_read.status())
        << "Failed to read elements of null-terminated array from sandbox at 0x"
        << absl::Hex(cur_ptr);
    size_t elems_read = *bytes_read / elem_size;
    CHECK_GT(elems_read, 0u)
        << "Short read of null-terminated array element from sandbox at 0x"
        << absl::Hex(cur_ptr);

    for (size_t i = 0; i < elems_read; ++i) {
      const char* elem_ptr = batch_buf.data() + i * elem_size;
      bool all_zero = is_zero_elem != nullptr
                          ? is_zero_elem(elem_ptr)
                          : std::all_of(elem_ptr, elem_ptr + elem_size,
                                        [](char b) { return b == 0; });
      CHECK_LE(result.size() + elem_size, kMaxNullTerminatedBytes)
          << "Null-terminated array at 0x" << absl::Hex(remote_ptr)
          << " exceeds maximum size (" << kMaxNullTerminatedBytes << " bytes)";
      result.insert(result.end(), elem_ptr, elem_ptr + elem_size);
      if (all_zero) {
        return result;
      }
    }
    cur_ptr += elems_read * elem_size;
  }
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

SandboxeeCallbackRegistry* SandboxeeCallbackRegistry::Instance() {
  static SandboxeeCallbackRegistry* instance = new SandboxeeCallbackRegistry();
  return instance;
}

void SandboxeeCallbackRegistry::PurgeStaleSessionLocked(
    const sapi::SandboxBase& sandbox) {
  const sapi::RPCChannel* channel = sandbox.rpc_channel();
  if (channel == nullptr) {
    return;
  }
  const uint64_t session_id = channel->session_id();
  auto [it, inserted] = active_session_.try_emplace(&sandbox, session_id);
  if (inserted || it->second == session_id) {
    return;
  }
  it->second = session_id;
  absl::erase_if(by_target_, [&sandbox](const auto& entry) {
    return std::get<1>(entry.first) == &sandbox;
  });
  absl::erase_if(by_thunk_, [&sandbox](const auto& entry) {
    return entry.second.sandbox == &sandbox;
  });
}

absl::StatusOr<uintptr_t> SandboxeeCallbackRegistry::ThunkFor(
    const sapi::SandboxBase& sandbox, const ThunkPool& pool, uintptr_t fn) {
  // Null is not a callback. Substituting a thunk for it would hand the caller
  // a non-null address and break every `if (cb)` guard in the library.
  if (fn == 0) {
    return 0;
  }

  absl::MutexLock lock(mutex_);
  PurgeStaleSessionLocked(sandbox);
  const TargetKey key{&pool, &sandbox, fn};
  if (auto it = by_target_.find(key); it != by_target_.end()) {
    return it->second;
  }

  for (uintptr_t thunk : pool.thunks) {
    if (by_thunk_.contains(thunk)) {
      continue;
    }
    by_thunk_.emplace(thunk, Binding{&pool, &sandbox, fn});
    by_target_.emplace(key, thunk);
    return thunk;
  }

  return absl::ResourceExhaustedError(absl::StrCat(
      "All ", pool.thunks.size(), " host thunks for callback typedef ",
      pool.typedef_name,
      " are in use. Every one stands in for a sandboxee callback that is still "
      "reachable, so none can be reused"));
}

std::optional<uintptr_t> SandboxeeCallbackRegistry::LookUpThunk(
    const sapi::SandboxBase& sandbox, const ThunkPool& pool, uintptr_t thunk) {
  if (thunk == 0) {
    return std::nullopt;
  }
  absl::MutexLock lock(mutex_);
  PurgeStaleSessionLocked(sandbox);
  auto it = by_thunk_.find(thunk);
  if (it == by_thunk_.end()) {
    return std::nullopt;
  }
  CHECK_EQ(it->second.pool, &pool)
      << "Host thunk bound for callback typedef '"
      << it->second.pool->typedef_name
      << "' used across a boundary expecting callback typedef '"
      << pool.typedef_name << "'";
  CHECK_EQ(it->second.sandbox, &sandbox)
      << "Host thunk bound for callback typedef '" << pool.typedef_name
      << "' used with a different sandbox instance";
  return it->second.fn;
}

void SandboxeeCallbackRegistry::Release(uintptr_t thunk) {
  if (thunk == 0) {
    return;
  }
  absl::MutexLock lock(mutex_);
  auto it = by_thunk_.find(thunk);
  CHECK(it != by_thunk_.end())
      << "Attempted to release an unknown or already released host thunk";
  by_target_.erase(
      TargetKey{it->second.pool, it->second.sandbox, it->second.fn});
  by_thunk_.erase(it);
}

void SandboxeeCallbackRegistry::ForgetSandbox(
    const sapi::SandboxBase& sandbox) {
  absl::MutexLock lock(mutex_);
  active_session_.erase(&sandbox);
  absl::erase_if(by_target_, [&sandbox](const auto& entry) {
    return std::get<1>(entry.first) == &sandbox;
  });
  absl::erase_if(by_thunk_, [&sandbox](const auto& entry) {
    return entry.second.sandbox == &sandbox;
  });
}

HostCallbackRegistry* HostCallbackRegistry::Instance() {
  static absl::NoDestructor<HostCallbackRegistry> instance;
  return instance.get();
}

void HostCallbackRegistry::PurgeStaleSessionLocked(
    const sapi::SandboxBase& sandbox, uint64_t session_id) {
  auto [it, inserted] = active_session_.try_emplace(&sandbox, session_id);
  if (inserted || it->second == session_id) {
    return;
  }
  if (session_id < it->second) {
    return;
  }
  it->second = session_id;
  absl::erase_if(by_target_, [&sandbox, session_id](const auto& entry) {
    return entry.first.sandbox == &sandbox &&
           entry.first.session_id != session_id;
  });
  absl::erase_if(by_trampoline_, [&sandbox, session_id](const auto& entry) {
    return entry.first.sandbox == &sandbox &&
           entry.first.session_id != session_id;
  });
}

absl::StatusOr<uintptr_t> HostCallbackRegistry::TrampolineForFactory(
    sapi::SandboxBase& sandbox, absl::string_view typedef_name, uintptr_t fn,
    bool idempotent, size_t stack_arg_count, HandlerFactory make_handler) {
  // Null is not a callback: keep it null and consume no trampoline slot.
  if (fn == 0) {
    return 0;
  }

  sapi::RPCChannel* channel = sandbox.rpc_channel();
  CHECK_NE(channel, nullptr)
      << "Sandbox RPCChannel is null while registering callback for typedef "
      << typedef_name;
  const uint64_t session_id = channel->session_id();
  const TargetKey key{&sandbox, session_id, std::string(typedef_name), fn};
  {
    absl::MutexLock lock(mutex_);
    PurgeStaleSessionLocked(sandbox, session_id);
    if (auto it = by_target_.find(key); it != by_target_.end()) {
      auto tramp_it =
          by_trampoline_.find(TrampolineKey{&sandbox, session_id, it->second});
      if (tramp_it != by_trampoline_.end() &&
          tramp_it->second.state != nullptr) {
        tramp_it->second.state->idempotent.store(idempotent,
                                                 std::memory_order_release);
        tramp_it->second.state->called.store(false, std::memory_order_release);
      }
      return it->second;
    }
  }

  auto state = std::make_shared<CallState>();
  state->idempotent.store(idempotent, std::memory_order_relaxed);
  RawHandler handler = std::move(make_handler)(state);
  absl::StatusOr<uintptr_t> trampoline =
      stack_arg_count == 0 ? channel->RegisterCallback(std::move(handler))
                           : channel->RegisterCallbackWithStackArgs(
                                 std::move(handler), stack_arg_count);
  if (!trampoline.ok()) {
    return trampoline.status();
  }

  uintptr_t winner = 0;
  {
    absl::MutexLock lock(mutex_);
    PurgeStaleSessionLocked(sandbox, session_id);
    auto session_it = active_session_.find(&sandbox);
    if (session_it != active_session_.end() &&
        session_it->second == session_id) {
      auto [it, inserted] = by_target_.emplace(key, *trampoline);
      if (inserted) {
        by_trampoline_.emplace(
            TrampolineKey{&sandbox, session_id, *trampoline},
            Binding{std::string(typedef_name), fn, std::move(state)});
        return *trampoline;
      }
      winner = it->second;
      auto tramp_it =
          by_trampoline_.find(TrampolineKey{&sandbox, session_id, winner});
      if (tramp_it != by_trampoline_.end() &&
          tramp_it->second.state != nullptr) {
        tramp_it->second.state->idempotent.store(idempotent,
                                                 std::memory_order_release);
        tramp_it->second.state->called.store(false, std::memory_order_release);
      }
    }
  }
  // Another thread registered the same (sandbox, session, typedef, fn)
  // concurrently, or the sandbox restarted mid-registration. Unregister the
  // duplicate/stale slot outside the lock.
  channel->UnregisterCallback(*trampoline).IgnoreError();
  if (winner == 0) {
    return absl::UnavailableError(
        "Sandbox session was invalidated during callback registration");
  }
  return winner;
}

std::optional<uintptr_t> HostCallbackRegistry::LookUpTrampoline(
    const sapi::SandboxBase& sandbox, absl::string_view typedef_name,
    uintptr_t trampoline) {
  // NOMUTANTS(ROR): Lock-free fast path for null function pointers; 0 is never
  // registered in by_trampoline_.
  if (trampoline == 0) {
    return std::nullopt;
  }
  sapi::RPCChannel* channel = sandbox.rpc_channel();
  if (channel == nullptr) {
    return std::nullopt;
  }
  const uint64_t session_id = channel->session_id();
  absl::MutexLock lock(mutex_);
  PurgeStaleSessionLocked(sandbox, session_id);
  auto it =
      by_trampoline_.find(TrampolineKey{&sandbox, session_id, trampoline});
  if (it == by_trampoline_.end()) {
    return std::nullopt;
  }
  // Security invariant: a compromised sandboxee could take a trampoline
  // address registered under `typedef_A` (e.g. `void (*)(int)`) and return it
  // through a return value or struct field governed by `typedef_B` (e.g.
  // `void (*)(const char*, size_t)`). Unwrapping without verifying that the
  // callback typedef matches would `reinterpret_cast` the `typedef_A` host
  // function pointer to `typedef_B`'s C ABI type, causing host-side
  // argument/type confusion if the host calls the pointer directly.
  CHECK_EQ(it->second.typedef_name, typedef_name)
      << "Sandboxee returned a host callback trampoline registered for "
         "callback typedef '"
      << it->second.typedef_name
      << "' across a boundary expecting callback typedef '" << typedef_name
      << "'";
  return it->second.fn;
}

void HostCallbackRegistry::Release(sapi::SandboxBase& sandbox,
                                   uintptr_t trampoline) {
  if (trampoline == 0) {
    return;
  }
  sapi::RPCChannel* channel = sandbox.rpc_channel();
  {
    absl::MutexLock lock(mutex_);
    auto it = by_trampoline_.end();
    if (channel != nullptr) {
      const uint64_t session_id = channel->session_id();
      PurgeStaleSessionLocked(sandbox, session_id);
      it = by_trampoline_.find(TrampolineKey{&sandbox, session_id, trampoline});
    } else {
      it = std::find_if(by_trampoline_.begin(), by_trampoline_.end(),
                        [&sandbox, trampoline](const auto& entry) {
                          return entry.first.sandbox == &sandbox &&
                                 entry.first.trampoline == trampoline;
                        });
    }
    CHECK(it != by_trampoline_.end())
        << "Attempted to release an unknown or already released host "
           "callback trampoline";
    by_target_.erase(TargetKey{&sandbox, it->first.session_id,
                               it->second.typedef_name, it->second.fn});
    by_trampoline_.erase(it);
  }
  if (channel != nullptr) {
    channel->UnregisterCallback(trampoline).IgnoreError();
  }
}

void HostCallbackRegistry::ForgetSandbox(sapi::SandboxBase& sandbox) {
  sapi::RPCChannel* channel = sandbox.rpc_channel();
  const std::optional<uint64_t> current_session =
      channel != nullptr ? std::optional<uint64_t>(channel->session_id())
                         : std::nullopt;
  std::vector<uintptr_t> trampolines_to_unregister;
  {
    absl::MutexLock lock(mutex_);
    active_session_.erase(&sandbox);
    absl::erase_if(by_target_, [&sandbox](const auto& entry) {
      return entry.first.sandbox == &sandbox;
    });
    for (const auto& [key, binding] : by_trampoline_) {
      if (key.sandbox == &sandbox && current_session.has_value() &&
          key.session_id == *current_session) {
        trampolines_to_unregister.push_back(key.trampoline);
      }
    }
    absl::erase_if(by_trampoline_, [&sandbox](const auto& entry) {
      return entry.first.sandbox == &sandbox;
    });
  }
  if (channel != nullptr) {
    for (uintptr_t trampoline : trampolines_to_unregister) {
      channel->UnregisterCallback(trampoline).IgnoreError();
    }
  }
}

}  // namespace lwbox
}  // namespace sapi
