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

// Runtime support shared by LWBox generated glue and hand-written thunks.
//
// Most sandboxed APIs are handled declaratively by the SANDBOX_* annotations
// in sandboxed_api/annotations.h. A minority of C interfaces need hand-written
// procedural glue to bridge host and sandboxee; those are "thunks"
// (SANDBOX_HOST_THUNK / SANDBOX_SANDBOXEE_THUNK).
//
// Marshaling a buffer for one call is emitted inline by the generator. What
// cannot be expressed that way is state that has to outlive a single call:
//
//   - ContextBindingRegistry: sizes, remote buffers and host copies keyed by
//     an opaque context pointer, so that a value produced by one call can be
//     consumed, reused or freed by a later one. This is what implements
//     SANDBOX_BIND_SIZE, SANDBOX_BYTE_SIZED_BY_BINDING,
//     SANDBOX_RETAIN_AND_BIND, SANDBOX_COPY_FROM_AND_BIND_OUT_PTR and
//     SANDBOX_CLEAR_BINDINGS.
//   - GlobalStringRegistry: caches C strings with global lifetime
//     (SANDBOX_LIFETIME_GLOBAL) read out of the sandboxee, which have to stay
//     valid for the lifetime of the process.
//
// Keeping that state here rather than inlining it into every generated
// wrapper keeps the emitted code readable, and makes the bookkeeping
// unit-testable against a mock RPC channel instead of verifiable only by
// golden-file comparison.
//
// It also keeps the bookkeeping reusable under LFI (Lightweight Fault
// Isolation, in-process sandboxing), where there is no sandbox2 process, no
// sapi::v:: transfer variables and no RPC channel: what is stored is plain
// addresses and sizes, and only the entry points that actually free or copy
// memory (ClearBindings, SyncFromSandboxToHost and GetOrFetch) name
// sapi::SandboxBase.
//
// Per-call buffer marshaling is deliberately not abstracted here yet. The
// generated code emits sapi::v::Array and rpc_channel() calls directly; a
// buffer type belongs here once the generator uses it, so that its shape is
// driven by the code it has to replace rather than guessed at.

#ifndef SANDBOXED_API_LWBOX_RUNTIME_LWBOX_RUNTIME_H_
#define SANDBOXED_API_LWBOX_RUNTIME_LWBOX_RUNTIME_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/node_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "sandboxed_api/sandbox.h"

namespace sapi {
namespace lwbox {

// Thread-safe runtime registry for context-bound parameter sizes and retained
// buffers.
//
// Exists because some C APIs split a value's producer and consumer across
// separate calls: a size or handle is established by one call and is only
// needed by a later one (e.g. opus_encoder_create() yields an encoder whose
// frame size a subsequent opus_encode() must know). There is no single call
// scope that can own such a value, so it is keyed here on the opaque context
// pointer that ties the calls together and released by ClearBindings().
class ContextBindingRegistry {
 public:
  static ContextBindingRegistry* Instance();

  // Binds a primitive size or length value to a context pointer + label.
  // Returns the previously bound size if one existed, or std::nullopt if newly
  // bound.
  // CHECK-fails if context is null.
  std::optional<size_t> BindSize(const void* absl_nonnull context,
                                 absl::string_view name, size_t size);

  // Retrieves a previously bound primitive size.
  // CHECK-fails if missing or if context is null.
  size_t GetSize(const void* absl_nonnull context, absl::string_view name);

  // Retains a remote buffer pointer associated with {context, name}.
  // If remote_ptr == 0, this call is safely treated as a no-op.
  // CHECK-fails if context is null or if a different remote pointer is already
  // retained for {context, name} without prior ClearBindings.
  void RetainPointer(const void* absl_nonnull context, absl::string_view name,
                     uintptr_t remote_ptr, size_t bytes);

  // Clears all bindings and retained remote buffers for a given context
  // pointer. Safely no-ops if context is null (matching free(nullptr)
  // teardown semantics).
  // CHECK-fails if the sandbox RPCChannel is null when remote buffers are
  // retained or if freeing retained remote buffers fails.
  void ClearBindings(sapi::SandboxBase& sandbox,
                     const void* absl_nullable context);

  struct BoundHostBuffer {
    uintptr_t remote_ptr = 0;
    void* host_ptr = nullptr;
    size_t bytes = 0;
  };

  // Looks up an existing bound host copy buffer for {context, name}.
  // Returns std::nullopt if no buffer is bound. CHECK-fails if context is null.
  std::optional<BoundHostBuffer> GetBoundHostBuffer(
      const void* absl_nonnull context, absl::string_view name);

  // Binds a newly allocated host copy buffer to {context, name}, freeing any
  // previous host copy buffer for this binding. Takes ownership of `host_ptr`,
  // which must be allocated with `std::malloc` (as it will be freed with
  // `std::free`).
  // CHECK-fails if context or host_ptr is null.
  void BindHostBuffer(const void* absl_nonnull context, absl::string_view name,
                      uintptr_t remote_ptr, void* absl_nonnull host_ptr,
                      size_t bytes);

 private:
  ContextBindingRegistry() = default;

  struct RetainedBuffer {
    uintptr_t remote_ptr;
    size_t bytes;
  };

  absl::Mutex mutex_;
  absl::node_hash_map<std::pair<const void*, std::string>, size_t> sizes_
      ABSL_GUARDED_BY(mutex_);
  absl::node_hash_map<std::pair<const void*, std::string>, RetainedBuffer>
      retained_ ABSL_GUARDED_BY(mutex_);
  absl::node_hash_map<std::pair<const void*, std::string>, BoundHostBuffer>
      bound_host_buffers_ ABSL_GUARDED_BY(mutex_);
};

// Thread-safe registry for caching remote static/global C-strings in host
// memory. Fetches the null-terminated string from the sandbox once and caches
// the result so that returned pointers remain valid for the lifetime of the
// process.
class GlobalStringRegistry {
 public:
  static GlobalStringRegistry* Instance();

  // Returns a pointer to the cached host C-string for the given remote pointer.
  // If not already cached, fetches the string from the sandbox via
  // sandbox.GetCString() and stores it.
  // Returns nullptr if remote_ptr is null.
  // CHECK-fails if fetching the string from the sandbox fails.
  const char* GetOrFetch(sapi::SandboxBase& sandbox, const void* remote_ptr);

 private:
  GlobalStringRegistry() = default;

  absl::Mutex mutex_;
  absl::node_hash_map<const void*, std::string> strings_
      ABSL_GUARDED_BY(mutex_);
};

// Helper to copy a range of memory from a remote sandbox pointer directly to
// host memory. Safely no-ops if bytes == 0, remote_ptr == 0, or host_ptr is
// null.
// CHECK-fails if the sandbox RPCChannel is null or if copying from the sandbox
// fails.
void SyncFromSandboxToHost(sapi::SandboxBase& sandbox, uintptr_t remote_ptr,
                           void* host_ptr, size_t bytes);

}  // namespace lwbox
}  // namespace sapi

#endif  // SANDBOXED_API_LWBOX_RUNTIME_LWBOX_RUNTIME_H_
