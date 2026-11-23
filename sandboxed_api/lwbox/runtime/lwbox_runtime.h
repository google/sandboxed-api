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
//   - SandboxeeCallbackRegistry: which host thunk stands in for which
//     sandboxee function, so that a callback the sandboxee implements can be
//     held, compared and called by host code that only understands host
//     addresses.
//   - HostCallbackRegistry: which sandboxee trampoline stands in for which
//     host function, so that a callback the host implements can outlive the
//     call that installed it and be recovered when handed back out.
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

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "sandboxed_api/sandbox.h"
#include "sandboxed_api/var_callback.h"

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

  // TODO(b/491828958): Consider also recording `void* host_ptr` here if we
  // chain `SANDBOX_RETAIN_AND_BIND` into `SANDBOX_COPY_FROM_AND_BIND_OUT_PTR`
  // (e.g., `cairo_image_surface_create_for_data` -> `..._get_data`), so a
  // getter returning `remote_ptr + offset` can sync into and return
  // `host_ptr + offset` instead of allocating a second host copy.
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

// A callback typedef's pool of host thunks.
//
// Generated code defines exactly one of these per callback typedef that can
// receive a sandboxee-implemented callback, and the pool's *address* is that
// typedef's identity here. `thunks` holds the addresses of the generated
// thunk functions, which all have the typedef's signature.
//
// The pool is shared by every sandbox in the process, because the thunks are
// static symbols in the host binary and there is only one of each. Its size is
// therefore a bound on how many sandboxee callbacks of this one typedef can be
// live at the same time across all sandboxes, not per sandbox.
//
// A pool must outlive every binding made through it, and must not be a
// temporary: the registry keeps its address as the typedef's identity.
struct ThunkPool {
  // Only used in diagnostics.
  absl::string_view typedef_name;
  absl::Span<const uintptr_t> thunks;
};

// Thread-safe registry mapping sandboxee functions to the host thunks that
// stand in for them, and back.
//
// Security model (Capability Table Indirection):
// A function pointer arriving from an untrusted sandboxee (via a return value
// or an `OUT`/`INOUT` struct field) must never be stored directly in a host
// function pointer variable, as calling a sandboxee-controlled address on the
// host would allow arbitrary host control-flow hijacking (CFI bypass). Instead,
// every incoming sandboxee address is bound here to one of a fixed set of
// compiler-emitted static host thunk functions (`sapi_thunk_<typedef>_0..7`)
// living in host `.text`. When called by the host, the thunk looks up its
// target address here and dispatches the call into the sandboxee via RPC, so
// the untrusted address is only ever jumped to inside the sandboxee's own fault
// domain.
//
//   ThunkFor        sandboxee function -> host thunk   ("store and replace")
//   LookUpThunk     host thunk -> sandboxee function   ("replace back")
//
// `LookUpThunk` returning nothing is also how a caller tells a genuine host
// function apart from a stand-in when a pointer crosses back into the sandbox.
class SandboxeeCallbackRegistry {
 public:
  static SandboxeeCallbackRegistry* Instance();

  // Returns the host thunk standing in for `fn` in `sandbox`, allocating one
  // from `pool` the first time this pair is seen and returning the same thunk
  // on every later call.
  //
  // A null `fn` is not a callback: it is returned unchanged and consumes no
  // slot, so that a library's `if (s->cb)` keeps working.
  //
  // Returns ResourceExhausted, naming the callback typedef, when all thunks in
  // `pool` are in use. Reusing one would silently redirect a callback the
  // caller still holds, so this is deliberately an error rather than an
  // eviction.
  absl::StatusOr<uintptr_t> ThunkFor(const sapi::SandboxBase& sandbox,
                                     const ThunkPool& pool, uintptr_t fn);

  // The inverse of ThunkFor. Returns std::nullopt if `thunk` is 0 or not a
  // thunk this registry handed out, which means the value is a genuine host
  // function and needs no substitution.
  //
  // CHECK-fails if `thunk` is bound to a different callback typedef pool than
  // `pool` or to a different sandbox instance than `sandbox`.
  std::optional<uintptr_t> LookUpThunk(const sapi::SandboxBase& sandbox,
                                       const ThunkPool& pool, uintptr_t thunk);

  // Returns `thunk`'s slot to its pool. Safely no-ops on 0, and CHECK-fails if
  // `thunk` is not a currently bound thunk. Calling a released thunk is a
  // use-after-free of the binding and the generated thunk will CHECK-fail
  // rather than call a stale target.
  void Release(uintptr_t thunk);

  // Releases every slot held on behalf of `sandbox`. Call when a sandbox goes
  // away: its addresses become meaningless, and its slots would otherwise stay
  // held for the lifetime of the process.
  void ForgetSandbox(const sapi::SandboxBase& sandbox);

 private:
  struct Binding {
    const ThunkPool* pool;
    const sapi::SandboxBase* sandbox;
    uintptr_t fn;
  };

  // Identifies a target: the same sandboxee function reached through two
  // different callback typedefs needs two different thunks, because the thunks
  // have different signatures.
  using TargetKey =
      std::tuple<const ThunkPool*, const sapi::SandboxBase*, uintptr_t>;

  SandboxeeCallbackRegistry() = default;

  void PurgeStaleSessionLocked(const sapi::SandboxBase& sandbox)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  absl::Mutex mutex_;
  absl::flat_hash_map<uintptr_t, Binding> by_thunk_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<TargetKey, uintptr_t> by_target_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<const sapi::SandboxBase*, uint64_t> active_session_
      ABSL_GUARDED_BY(mutex_);
};

// Thread-safe registry mapping host callback functions to the sandboxee
// trampolines that stand in for them, and back.
//
// This is the mirror of SandboxeeCallbackRegistry for the forward direction:
// when host code hands one of its own functions to a library that keeps the
// pointer past the call that installed it (in a struct field or a global
// hook), a call-scoped `sapi::v::Callback` would unregister its trampoline as
// soon as the installing shim returned. Keying the registration here on
// `(sandbox, typedef_name, host_fn)` keeps the trampoline alive for the
// lifetime of the sandbox, ensures installing the same host function twice
// reuses one trampoline slot and preserves pointer equality inside the
// sandbox, and lets `LookUpTrampoline` recover the original host function
// address when the sandbox later hands the pointer back to the host.
//
// Security notes:
// 1. No host `.text` address ever enters the sandboxee: the sandboxee only
//    receives the address of a slot in its own `sapi_callback_trampolines`
//    array, and upcalls identify the slot by integer index `[0, 64)`.
// 2. Multiplicity / Idempotency: by default (`idempotent == false`), a
//    registered callback is armed for at most one invocation per registration
//    and `CHECK`-fails if the sandboxee invokes it a second time without the
//    host re-arming it via `TrampolineFor`. When opted in with
//    `idempotent == true` (`SANDBOX_CALLBACK_IDEMPOTENT`), the sandboxee can
//    invoke that trampoline an arbitrary number of times during any active RPC
//    call; generated marshaling wrappers allocate fresh host buffers per
//    invocation, and the user's host callback function must tolerate repeated
//    invocation without corrupting host state.
// 3. Callback-typedef-checked unwrapping: `LookUpTrampoline` verifies that the
//    trampoline's recorded `typedef_name` matches the callback typedef of the
//    return value or struct field through which it is leaving the sandbox,
//    preventing a compromised sandboxee from swapping trampolines across
//    callback typedefs with incompatible C ABI signatures.
class HostCallbackRegistry {
 public:
  struct CallState {
    std::atomic<bool> called{false};
    std::atomic<bool> idempotent{false};
  };

  using RawHandler = absl::AnyInvocable<uint64_t(absl::Span<const uint64_t>)>;
  using HandlerFactory =
      absl::AnyInvocable<RawHandler(std::shared_ptr<CallState>) &&>;

  static HostCallbackRegistry* Instance();

  // Returns the sandboxee trampoline address standing in for `fn` under
  // `typedef_name` in `sandbox`, registering `handler` via
  // `sandbox.rpc_channel()->RegisterCallback(...)` the first time this triple
  // is seen and returning the same trampoline address on every later call.
  //
  // By default (`idempotent == false`), the callback is armed for at most one
  // invocation and tombstoned (`CHECK`-failing on a second invocation from the
  // sandboxee) until the host calls `TrampolineFor` again to re-arm it. When
  // `idempotent == true` (`SANDBOX_CALLBACK_IDEMPOTENT`), the callback may be
  // invoked an arbitrary number of times.
  //
  // A null `fn` is not a callback: it is returned unchanged and consumes no
  // slot, so that a library's `if (s->cb)` keeps working.
  template <typename F>
  absl::StatusOr<uintptr_t> TrampolineFor(sapi::SandboxBase& sandbox,
                                          absl::string_view typedef_name,
                                          uintptr_t fn, F&& handler,
                                          bool idempotent = false) {
    using Traits = sapi::v::callback_internal::FunctionTraits<std::decay_t<F>>;
    static_assert(Traits::kArgsCount <= 6,
                  "SAPI callbacks support a maximum of 6 arguments");
    return TrampolineForFactory(
        sandbox, typedef_name, fn, idempotent,
        [f = std::forward<F>(handler),
         typedef_name](std::shared_ptr<CallState> state) mutable -> RawHandler {
          return [f = std::move(f), cb_typedef = std::string(typedef_name),
                  state = std::move(state)](
                     absl::Span<const uint64_t> span) mutable -> uint64_t {
            if (!state->idempotent.load(std::memory_order_acquire)) {
              CHECK(!state->called.exchange(true, std::memory_order_acq_rel))
                  << "Host callback for callback typedef '" << cb_typedef
                  << "' was invoked more than once without "
                     "SANDBOX_CALLBACK_IDEMPOTENT";
            }
            using ArgsTuple = typename Traits::ArgsTuple;
            return sapi::v::callback_internal::CallHelper<ArgsTuple>(f, span);
          };
        });
  }

  // The inverse of TrampolineFor. Returns std::nullopt if `trampoline` is not
  // a trampoline this registry handed out for `sandbox`, which means the value
  // is a genuine sandboxee function and needs a host thunk from
  // SandboxeeCallbackRegistry instead.
  //
  // CHECK-fails if `trampoline` WAS registered for `sandbox`, but under a
  // different callback typedef than `typedef_name` (preventing cross-typedef C
  // ABI type confusion if a compromised sandboxee substitutes a trampoline from
  // another callback typedef).
  std::optional<uintptr_t> LookUpTrampoline(const sapi::SandboxBase& sandbox,
                                            absl::string_view typedef_name,
                                            uintptr_t trampoline);

  // Unregisters `trampoline` in `sandbox` and removes its binding. Safely
  // no-ops on 0, and CHECK-fails if `trampoline` is not currently registered
  // for `sandbox`.
  void Release(sapi::SandboxBase& sandbox, uintptr_t trampoline);

  // Unregisters and removes every trampoline held on behalf of `sandbox`.
  void ForgetSandbox(sapi::SandboxBase& sandbox);

 private:
  friend class absl::NoDestructor<HostCallbackRegistry>;

  struct Binding {
    std::string typedef_name;
    uintptr_t fn;
    std::shared_ptr<CallState> state;
  };

  // The same host function reached through two different callback typedefs
  // needs two different trampolines because each callback typedef has its own
  // marshaling wrapper. Including `RPCChannel::session_id()` ensures restarting
  // a `SandboxBase` in place invalidates cached trampolines from the previous
  // sandboxee process even if `ForgetSandbox` was not called.
  struct TargetKey {
    const sapi::SandboxBase* sandbox;
    uint64_t session_id;
    std::string typedef_name;
    uintptr_t fn;

    friend bool operator==(const TargetKey&, const TargetKey&) = default;
    template <typename H>
    friend H AbslHashValue(H h, const TargetKey& key) {
      return H::combine(std::move(h), key.sandbox, key.session_id,
                        key.typedef_name, key.fn);
    }
  };

  struct TrampolineKey {
    const sapi::SandboxBase* sandbox;
    uint64_t session_id;
    uintptr_t trampoline;

    friend bool operator==(const TrampolineKey&,
                           const TrampolineKey&) = default;
    template <typename H>
    friend H AbslHashValue(H h, const TrampolineKey& key) {
      return H::combine(std::move(h), key.sandbox, key.session_id,
                        key.trampoline);
    }
  };

  HostCallbackRegistry() = default;

  absl::StatusOr<uintptr_t> TrampolineForFactory(sapi::SandboxBase& sandbox,
                                                 absl::string_view typedef_name,
                                                 uintptr_t fn, bool idempotent,
                                                 HandlerFactory make_handler);

  void PurgeStaleSessionLocked(const sapi::SandboxBase& sandbox,
                               uint64_t session_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  absl::Mutex mutex_;
  absl::flat_hash_map<TrampolineKey, Binding> by_trampoline_
      ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<TargetKey, uintptr_t> by_target_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<const sapi::SandboxBase*, uint64_t> active_session_
      ABSL_GUARDED_BY(mutex_);
};

// Helper to copy a range of memory from a remote sandbox pointer directly to
// host memory. Safely no-ops if bytes == 0, remote_ptr == 0, or host_ptr is
// null.
// CHECK-fails if the sandbox RPCChannel is null or if copying from the sandbox
// fails.
void SyncFromSandboxToHost(sapi::SandboxBase& sandbox, uintptr_t remote_ptr,
                           void* host_ptr, size_t bytes);

// Converts an integral size expression to `size_t`, aborting via `CHECK_GE` if
// a signed value is negative so untrusted signed sizes from a sandboxee cannot
// wrap around to huge `size_t` values.
template <typename T>
size_t CheckedToSizeT(T value) {
  static_assert(std::is_integral_v<T>,
                "CheckedToSizeT requires an integral type");
  if constexpr (std::is_signed_v<T>) {
    CHECK_GE(value, 0) << "Negative buffer size: " << value;
  }
  return static_cast<size_t>(value);
}

namespace internal {

inline bool AllBytesZero(const void* ptr, size_t size) {
  const auto* bytes = static_cast<const unsigned char*>(ptr);
  for (size_t i = 0; i < size; ++i) {
    if (bytes[i] != 0) {
      return false;
    }
  }
  return true;
}

// Probes the direct aggregate initializer arity (up to 16 fields) of a class
// aggregate `T` using empty `{}` clauses. An empty `{}` initializes a single
// direct member (including embedded C arrays) without brace elision.
template <typename T>
consteval size_t AggregateFieldCount() {
  if constexpr (!std::is_class_v<T> || !std::is_aggregate_v<T>) {
    return 0;
  } else if constexpr (requires {
                         T{{}, {}, {}, {}, {}, {}, {}, {}, {},
                           {}, {}, {}, {}, {}, {}, {}, {}};
                       }) {
    return 17;  // > 16 direct aggregate elements.
  } else if constexpr (requires {
                         T{{}, {}, {}, {}, {}, {}, {}, {},
                           {}, {}, {}, {}, {}, {}, {}, {}};
                       }) {
    return 16;
  } else if constexpr (requires {
                         T{{}, {}, {}, {}, {}, {}, {}, {},
                           {}, {}, {}, {}, {}, {}, {}};
                       }) {
    return 15;
  } else if constexpr (requires {
                         T{{}, {}, {}, {}, {}, {}, {},
                           {}, {}, {}, {}, {}, {}, {}};
                       }) {
    return 14;
  } else if constexpr (requires {
                         T{{}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}};
                       }) {
    return 13;
  } else if constexpr (requires {
                         T{{}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}};
                       }) {
    return 12;
  } else if constexpr (requires {
                         T{{}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}};
                       }) {
    return 11;
  } else if constexpr (requires {
                         T{{}, {}, {}, {}, {}, {}, {}, {}, {}, {}};
                       }) {
    return 10;
  } else if constexpr (requires { T{{}, {}, {}, {}, {}, {}, {}, {}, {}}; }) {
    return 9;
  } else if constexpr (requires { T{{}, {}, {}, {}, {}, {}, {}, {}}; }) {
    return 8;
  } else if constexpr (requires { T{{}, {}, {}, {}, {}, {}, {}}; }) {
    return 7;
  } else if constexpr (requires { T{{}, {}, {}, {}, {}, {}}; }) {
    return 6;
  } else if constexpr (requires { T{{}, {}, {}, {}, {}}; }) {
    return 5;
  } else if constexpr (requires { T{{}, {}, {}, {}}; }) {
    return 4;
  } else if constexpr (requires { T{{}, {}, {}}; }) {
    return 3;
  } else if constexpr (requires { T{{}, {}}; }) {
    return 2;
  } else if constexpr (requires { T{{}}; }) {
    return 1;
  } else {
    return 0;
  }
}

template <typename T>
bool IsZeroElement(const T& elem);

// Portable C++20 field-by-field zero check for standard-layout C struct
// aggregates with 1..16 direct fields (used on toolchains such as Clang < 23
// that do not yet implement `__builtin_clear_padding`).
template <typename T>
bool AggregateMemberwiseIsZero(const T& elem) {
  constexpr size_t kArity = AggregateFieldCount<T>();
  static_assert(kArity >= 1 && kArity <= 16,
                "AggregateMemberwiseIsZero requires 1..16 aggregate fields");
  if constexpr (kArity == 1) {
    const auto& [f0] = elem;
    return IsZeroElement(f0);
  } else if constexpr (kArity == 2) {
    const auto& [f0, f1] = elem;
    return IsZeroElement(f0) && IsZeroElement(f1);
  } else if constexpr (kArity == 3) {
    const auto& [f0, f1, f2] = elem;
    return IsZeroElement(f0) && IsZeroElement(f1) && IsZeroElement(f2);
  } else if constexpr (kArity == 4) {
    const auto& [f0, f1, f2, f3] = elem;
    return IsZeroElement(f0) && IsZeroElement(f1) && IsZeroElement(f2) &&
           IsZeroElement(f3);
  } else if constexpr (kArity == 5) {
    const auto& [f0, f1, f2, f3, f4] = elem;
    return IsZeroElement(f0) && IsZeroElement(f1) && IsZeroElement(f2) &&
           IsZeroElement(f3) && IsZeroElement(f4);
  } else if constexpr (kArity == 6) {
    const auto& [f0, f1, f2, f3, f4, f5] = elem;
    return IsZeroElement(f0) && IsZeroElement(f1) && IsZeroElement(f2) &&
           IsZeroElement(f3) && IsZeroElement(f4) && IsZeroElement(f5);
  } else if constexpr (kArity == 7) {
    const auto& [f0, f1, f2, f3, f4, f5, f6] = elem;
    return IsZeroElement(f0) && IsZeroElement(f1) && IsZeroElement(f2) &&
           IsZeroElement(f3) && IsZeroElement(f4) && IsZeroElement(f5) &&
           IsZeroElement(f6);
  } else if constexpr (kArity == 8) {
    const auto& [f0, f1, f2, f3, f4, f5, f6, f7] = elem;
    return IsZeroElement(f0) && IsZeroElement(f1) && IsZeroElement(f2) &&
           IsZeroElement(f3) && IsZeroElement(f4) && IsZeroElement(f5) &&
           IsZeroElement(f6) && IsZeroElement(f7);
  } else if constexpr (kArity == 9) {
    const auto& [f0, f1, f2, f3, f4, f5, f6, f7, f8] = elem;
    return IsZeroElement(f0) && IsZeroElement(f1) && IsZeroElement(f2) &&
           IsZeroElement(f3) && IsZeroElement(f4) && IsZeroElement(f5) &&
           IsZeroElement(f6) && IsZeroElement(f7) && IsZeroElement(f8);
  } else if constexpr (kArity == 10) {
    const auto& [f0, f1, f2, f3, f4, f5, f6, f7, f8, f9] = elem;
    return IsZeroElement(f0) && IsZeroElement(f1) && IsZeroElement(f2) &&
           IsZeroElement(f3) && IsZeroElement(f4) && IsZeroElement(f5) &&
           IsZeroElement(f6) && IsZeroElement(f7) && IsZeroElement(f8) &&
           IsZeroElement(f9);
  } else if constexpr (kArity == 11) {
    const auto& [f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10] = elem;
    return IsZeroElement(f0) && IsZeroElement(f1) && IsZeroElement(f2) &&
           IsZeroElement(f3) && IsZeroElement(f4) && IsZeroElement(f5) &&
           IsZeroElement(f6) && IsZeroElement(f7) && IsZeroElement(f8) &&
           IsZeroElement(f9) && IsZeroElement(f10);
  } else if constexpr (kArity == 12) {
    const auto& [f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11] = elem;
    return IsZeroElement(f0) && IsZeroElement(f1) && IsZeroElement(f2) &&
           IsZeroElement(f3) && IsZeroElement(f4) && IsZeroElement(f5) &&
           IsZeroElement(f6) && IsZeroElement(f7) && IsZeroElement(f8) &&
           IsZeroElement(f9) && IsZeroElement(f10) && IsZeroElement(f11);
  } else if constexpr (kArity == 13) {
    const auto& [f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12] = elem;
    return IsZeroElement(f0) && IsZeroElement(f1) && IsZeroElement(f2) &&
           IsZeroElement(f3) && IsZeroElement(f4) && IsZeroElement(f5) &&
           IsZeroElement(f6) && IsZeroElement(f7) && IsZeroElement(f8) &&
           IsZeroElement(f9) && IsZeroElement(f10) && IsZeroElement(f11) &&
           IsZeroElement(f12);
  } else if constexpr (kArity == 14) {
    const auto& [f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13] =
        elem;
    return IsZeroElement(f0) && IsZeroElement(f1) && IsZeroElement(f2) &&
           IsZeroElement(f3) && IsZeroElement(f4) && IsZeroElement(f5) &&
           IsZeroElement(f6) && IsZeroElement(f7) && IsZeroElement(f8) &&
           IsZeroElement(f9) && IsZeroElement(f10) && IsZeroElement(f11) &&
           IsZeroElement(f12) && IsZeroElement(f13);
  } else if constexpr (kArity == 15) {
    const auto& [f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13,
                 f14] = elem;
    return IsZeroElement(f0) && IsZeroElement(f1) && IsZeroElement(f2) &&
           IsZeroElement(f3) && IsZeroElement(f4) && IsZeroElement(f5) &&
           IsZeroElement(f6) && IsZeroElement(f7) && IsZeroElement(f8) &&
           IsZeroElement(f9) && IsZeroElement(f10) && IsZeroElement(f11) &&
           IsZeroElement(f12) && IsZeroElement(f13) && IsZeroElement(f14);
  } else {
    const auto& [f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13,
                 f14, f15] = elem;
    return IsZeroElement(f0) && IsZeroElement(f1) && IsZeroElement(f2) &&
           IsZeroElement(f3) && IsZeroElement(f4) && IsZeroElement(f5) &&
           IsZeroElement(f6) && IsZeroElement(f7) && IsZeroElement(f8) &&
           IsZeroElement(f9) && IsZeroElement(f10) && IsZeroElement(f11) &&
           IsZeroElement(f12) && IsZeroElement(f13) && IsZeroElement(f14) &&
           IsZeroElement(f15);
  }
}

// Tests whether `elem` is a terminating zero sentinel while ignoring
// uninitialized alignment or bit-field padding. Uses `__builtin_clear_padding`
// when available, and falls back to C++20 structured-binding memberwise
// recursion (up to 16 fields) on older Clang releases that lack the builtin.
template <typename T>
bool IsZeroElement(const T& elem) {
  using ValueT = std::remove_cv_t<T>;
  if constexpr (std::is_array_v<ValueT>) {
    for (const auto& sub : elem) {
      if (!IsZeroElement(sub)) {
        return false;
      }
    }
    return true;
  } else if constexpr (requires(const ValueT& a) {
                         requires std::is_convertible_v<decltype(a == ValueT{}),
                                                        bool>;
                       }) {
    return elem == ValueT{};
  } else if constexpr (std::has_unique_object_representations_v<ValueT>) {
    return AllBytesZero(std::addressof(elem), sizeof(ValueT));
  } else if constexpr (std::is_empty_v<ValueT>) {
    return true;
  } else {
#if __has_builtin(__builtin_clear_padding)
    alignas(ValueT) unsigned char raw[sizeof(ValueT)];
    std::memcpy(raw, std::addressof(elem), sizeof(ValueT));
    auto* copy_ptr = reinterpret_cast<ValueT*>(raw);
    __builtin_clear_padding(copy_ptr);
    return AllBytesZero(copy_ptr, sizeof(ValueT));
#else
    constexpr size_t kArity = AggregateFieldCount<ValueT>();
    if constexpr (std::is_standard_layout_v<ValueT> && kArity >= 1 &&
                  kArity <= 16) {
      return AggregateMemberwiseIsZero(elem);
    } else {
      static_assert(
          sizeof(ValueT) == 0,
          "Zero-sentinel-terminated struct with padding requires either "
          "__builtin_clear_padding (Clang >= 23 / GCC >= 11) or a "
          "standard-layout aggregate with at most 16 direct fields.");
      return false;
    }
#endif
  }
}

}  // namespace internal

// Returns the number of non-zero elements preceding the terminating zero
// element in a null-terminated array, or 0 if `ptr` is null. Supports scalar,
// enum, pointer, and aggregate struct element types (where the sentinel element
// has all fields equal to zero/nullptr).
template <typename T>
size_t NullTerminatedCount(const T* absl_nullable ptr) {
  if (ptr == nullptr) {
    return 0;
  }
  size_t count = 0;
  while (!internal::IsZeroElement(ptr[count])) {
    ++count;
  }
  return count;
}

// Returns the size in bytes (including the terminating zero element) of a
// null-terminated array of scalar, enum, pointer, or struct elements, or 0 if
// `ptr` is null.
template <typename T>
size_t NullTerminatedByteSize(const T* absl_nullable ptr) {
  if (ptr == nullptr) {
    return 0;
  }
  return (NullTerminatedCount(ptr) + 1) * sizeof(T);
}

// Reads a zero-terminated array of elements of `elem_size` bytes from
// `remote_ptr` in `sandbox` into a freshly allocated host buffer (including
// the trailing zero element). When `is_zero_elem` is non-null, it is used to
// test whether an element is the terminating zero sentinel (for example,
// ignoring uninitialized padding bytes in a C struct); otherwise all
// `elem_size` bytes must be zero.
// Returns an empty vector if `remote_ptr == 0`.
// CHECK-fails if `elem_size == 0`, if the sandbox RPCChannel is null, if
// reading from the sandbox fails, or if the array exceeds 10 MiB.
std::vector<char> ReadNullTerminatedArray(
    sapi::SandboxBase& sandbox, uintptr_t remote_ptr, size_t elem_size,
    bool (*absl_nullable is_zero_elem)(const void* elem) = nullptr);

// Typed overload of `ReadNullTerminatedArray` that uses
// `internal::IsZeroElement<std::remove_cv_t<T>>` to detect the terminating
// zero sentinel, ignoring any uninitialized struct or bit-field padding in a
// `{0, ...}` sentinel element. Single-byte types without padding bits
// (`std::has_unique_object_representations_v<ValueT>`, such as `char` and
// `uint8_t`) take the `GetCString` fast path directly, whereas 1-byte structs
// with unused bit-field padding bits go through `IsZeroElement` (and
// `__builtin_clear_padding`).
template <typename T>
std::vector<char> ReadNullTerminatedArray(sapi::SandboxBase& sandbox,
                                          uintptr_t remote_ptr) {
  using ValueT = std::remove_cv_t<T>;
  if constexpr (sizeof(ValueT) == 1 &&
                std::has_unique_object_representations_v<ValueT>) {
    return ReadNullTerminatedArray(sandbox, remote_ptr, 1);
  } else {
    return ReadNullTerminatedArray(
        sandbox, remote_ptr, sizeof(ValueT), +[](const void* elem) -> bool {
          alignas(ValueT) unsigned char raw[sizeof(ValueT)];
          std::memcpy(raw, elem, sizeof(ValueT));
          return internal::IsZeroElement(*reinterpret_cast<const ValueT*>(raw));
        });
  }
}

}  // namespace lwbox
}  // namespace sapi

#endif  // SANDBOXED_API_LWBOX_RUNTIME_LWBOX_RUNTIME_H_
