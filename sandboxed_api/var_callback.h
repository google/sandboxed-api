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

#ifndef SANDBOXED_API_VAR_CALLBACK_H_
#define SANDBOXED_API_VAR_CALLBACK_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/var_abstract.h"
#include "sandboxed_api/var_ptr.h"
#include "sandboxed_api/var_type.h"

namespace sapi::v {

namespace callback_internal {

template <typename T>
inline constexpr bool kIsSupportedCallbackScalar =
    std::is_integral_v<T> || std::is_enum_v<T> || std::is_pointer_v<T> ||
    std::is_same_v<T, RemotePtr> ||
    (std::is_floating_point_v<T> &&
     (sizeof(T) == sizeof(uint32_t) || sizeof(T) == sizeof(uint64_t)));

template <typename T>
std::decay_t<T> CastArg(uint64_t val) {
  using DecayedT = std::decay_t<T>;
  static_assert(kIsSupportedCallbackScalar<DecayedT>,
                "Callback arguments must be integral, enum, float/double, "
                "pointer, or RemotePtr types");
  if constexpr (std::is_pointer_v<DecayedT>) {
    return reinterpret_cast<DecayedT>(val);
  } else if constexpr (std::is_same_v<DecayedT, bool>) {
    // Under the x86_64 SysV and AArch64 AAPCS64 ABIs, a `bool` argument passed
    // in a register or 8-byte stack slot only guarantees a valid low byte; the
    // upper 56 bits of a raw 64-bit load may contain caller garbage.
    return static_cast<uint8_t>(val) != 0;
  } else if constexpr (std::is_enum_v<DecayedT>) {
    // Narrow to the enum's underlying integer type first so dirty upper bits in
    // 64-bit register or stack slots for 32-bit C enums do not corrupt values
    // or trigger `-fsanitize=enum` undefined behavior.
    using UnderlyingT = std::underlying_type_t<DecayedT>;
    if constexpr (std::is_same_v<UnderlyingT, bool>) {
      return static_cast<DecayedT>(static_cast<uint8_t>(val) != 0);
    } else {
      return static_cast<DecayedT>(static_cast<UnderlyingT>(val));
    }
  } else if constexpr (std::is_floating_point_v<DecayedT>) {
    if constexpr (sizeof(DecayedT) == sizeof(uint32_t)) {
      uint32_t bits = static_cast<uint32_t>(val);
      DecayedT out;
      std::memcpy(&out, &bits, sizeof(out));
      return out;
    } else {
      DecayedT out;
      std::memcpy(&out, &val, sizeof(out));
      return out;
    }
  } else {
    return static_cast<DecayedT>(val);
  }
}

template <typename T>
uint64_t CastRet(T val) {
  using DecayedT = std::decay_t<T>;
  static_assert(kIsSupportedCallbackScalar<DecayedT>,
                "Callback return values must be integral, enum, float/double, "
                "pointer, or RemotePtr types");
  if constexpr (std::is_pointer_v<DecayedT>) {
    return reinterpret_cast<uint64_t>(val);
  } else if constexpr (std::is_same_v<DecayedT, RemotePtr>) {
    return val.GetRemoteValue();
  } else if constexpr (std::is_floating_point_v<DecayedT>) {
    if constexpr (sizeof(DecayedT) == sizeof(uint32_t)) {
      uint32_t bits = 0;
      std::memcpy(&bits, &val, sizeof(bits));
#if defined(__riscv) && defined(__riscv_float_abi_double)
      // RISC-V LP64D requires 32-bit floats in 64-bit FP registers to be
      // NaN-boxed (upper 32 bits all ones) before `fmv.d.x fa0, a0`.
      return 0xffffffff00000000ULL | static_cast<uint64_t>(bits);
#else
      return bits;
#endif
    } else {
      uint64_t bits = 0;
      std::memcpy(&bits, &val, sizeof(bits));
      return bits;
    }
  } else {
    return static_cast<uint64_t>(val);
  }
}

// Adds the parameters `K..I-1` of `Tuple` to the per-class counters:
// floating-point parameters to `float_idx` and all other parameters to
// `int_idx`. On RISC-V, a floating-point parameter counts as an integer
// parameter once `float_idx` reaches `max_float_regs`, because RISC-V then
// passes it like an integer.
template <typename Tuple, size_t I, size_t K = 0>
constexpr void CountPriorArgClasses(size_t& int_idx, size_t& float_idx,
                                    size_t max_float_regs) {
  if constexpr (K < I) {
    using ElemT = std::decay_t<std::tuple_element_t<K, Tuple>>;
    if constexpr (std::is_floating_point_v<ElemT>) {
#if defined(__riscv)
      if (float_idx < max_float_regs) {
        ++float_idx;
      } else {
        ++int_idx;
      }
#else
      (void)max_float_regs;
      ++float_idx;
#endif
    } else {
      ++int_idx;
    }
    CountPriorArgClasses<Tuple, I, K + 1>(int_idx, float_idx, max_float_regs);
  }
}

// Number of `CallbackRequest::args` slots in the full callback layout that
// `ArgSlotIndex()` maps parameters to.
inline constexpr size_t kFullLayoutArgSlots =
    std::extent_v<decltype(CallbackRequest::args)>;

#if defined(__aarch64__) || defined(__riscv)
inline constexpr size_t kMaxCallbackIntRegs = 8;
#else
inline constexpr size_t kMaxCallbackIntRegs = 6;
#endif
#if defined(__riscv) && !defined(__riscv_float_abi_double)
inline constexpr size_t kMaxCallbackFloatRegs = 0;
#else
inline constexpr size_t kMaxCallbackFloatRegs = 8;
#endif

// Returns the number of 8-byte stack argument slots passed by the caller for
// `Tuple` under the target C ABI (`0..4` for `<= 10` scalar parameters).
template <typename Tuple>
constexpr size_t StackArgCount() {
  size_t int_idx = 0;
  size_t float_idx = 0;
  CountPriorArgClasses<Tuple, std::tuple_size_v<Tuple>>(int_idx, float_idx,
                                                        kMaxCallbackFloatRegs);
  return (int_idx > kMaxCallbackIntRegs ? int_idx - kMaxCallbackIntRegs : 0) +
         (float_idx > kMaxCallbackFloatRegs ? float_idx - kMaxCallbackFloatRegs
                                            : 0);
}

// Maps parameter `I` of `Tuple` to its slot in `CallbackRequest::args`:
//   [0..7]:   integer / pointer registers (6 on x86_64, 8 on AArch64 / RISC-V)
//   [8..15]:  floating-point registers (xmm0..xmm7 / d0..d7 / fa0..fa7)
//   [16..19]: overflow stack arguments
// A span with fewer than `kFullLayoutArgSlots` slots has the flat layout of an
// RPC channel that forwards only the first integer argument registers in order
// (for example 6 of them) or of a synthetic test, so this returns flat index
// `I`. `CallHelperFlat()` checks that the callback fits the flat layout.
template <typename Tuple, size_t I>
constexpr size_t ArgSlotIndex(size_t span_size) {
  if (span_size < kFullLayoutArgSlots) {
    return I;
  }
  size_t int_idx = 0;
  size_t float_idx = 0;
  CountPriorArgClasses<Tuple, I>(int_idx, float_idx, kMaxCallbackFloatRegs);
  size_t stack_idx =
      (int_idx > kMaxCallbackIntRegs ? int_idx - kMaxCallbackIntRegs : 0) +
      (float_idx > kMaxCallbackFloatRegs ? float_idx - kMaxCallbackFloatRegs
                                         : 0);
  using CurT = std::decay_t<std::tuple_element_t<I, Tuple>>;
  if constexpr (std::is_floating_point_v<CurT>) {
#if defined(__riscv)
    if (float_idx < kMaxCallbackFloatRegs) {
      return 8 + float_idx;
    }
    return int_idx < kMaxCallbackIntRegs ? int_idx : (16 + stack_idx);
#else
    return float_idx < kMaxCallbackFloatRegs ? (8 + float_idx)
                                             : (16 + stack_idx);
#endif
  } else {
    return int_idx < kMaxCallbackIntRegs ? int_idx : (16 + stack_idx);
  }
}

template <typename T>
struct FunctionTraits;

template <typename R, typename... Args>
struct FunctionTraits<R (*)(Args...)> {
  using ArgsTuple = std::tuple<Args...>;
  static constexpr size_t kArgsCount = sizeof...(Args);
};

template <typename R, typename... Args>
struct FunctionTraits<R (*)(Args...) noexcept> {
  using ArgsTuple = std::tuple<Args...>;
  static constexpr size_t kArgsCount = sizeof...(Args);
};

template <typename C, typename R, typename... Args>
struct FunctionTraits<R (C::*)(Args...) const> {
  using ArgsTuple = std::tuple<Args...>;
  static constexpr size_t kArgsCount = sizeof...(Args);
};

template <typename C, typename R, typename... Args>
struct FunctionTraits<R (C::*)(Args...) const noexcept> {
  using ArgsTuple = std::tuple<Args...>;
  static constexpr size_t kArgsCount = sizeof...(Args);
};

template <typename C, typename R, typename... Args>
struct FunctionTraits<R (C::*)(Args...)> {
  using ArgsTuple = std::tuple<Args...>;
  static constexpr size_t kArgsCount = sizeof...(Args);
};

template <typename C, typename R, typename... Args>
struct FunctionTraits<R (C::*)(Args...) noexcept> {
  using ArgsTuple = std::tuple<Args...>;
  static constexpr size_t kArgsCount = sizeof...(Args);
};

template <typename F>
struct FunctionTraits {
  using Traits = FunctionTraits<decltype(&F::operator())>;
  using ArgsTuple = typename Traits::ArgsTuple;
  static constexpr size_t kArgsCount = Traits::kArgsCount;
};

template <typename F, typename Tuple, size_t... Is>
uint64_t CallHelperFlat(F&& f, absl::Span<const uint64_t> span,
                        std::index_sequence<Is...>) {
  using ArgsTuple = Tuple;
  using R = std::invoke_result_t<F, std::tuple_element_t<Is, ArgsTuple>...>;
  if (span.size() < kFullLayoutArgSlots) {
    // The flat layout has no floating-point register slots and no stack
    // slots. Fail closed rather than read past `span` or use integer register
    // values as `float`/`double` arguments and return values.
    constexpr bool kUsesFloatingPoint =
        (std::is_floating_point_v<
             std::decay_t<std::tuple_element_t<Is, ArgsTuple>>> ||
         ...) ||
        std::is_floating_point_v<std::decay_t<R>>;
    CHECK_LE(sizeof...(Is), span.size())
        << "Callback takes " << sizeof...(Is)
        << " arguments, but the RPC channel passes only " << span.size();
    CHECK(!kUsesFloatingPoint)
        << "Callbacks with float or double arguments or return values need "
           "the full callback layout, which the RPC channel does not pass";
  }
  if constexpr (std::is_void_v<R>) {
    f(CastArg<std::tuple_element_t<Is, ArgsTuple>>(
        span[ArgSlotIndex<ArgsTuple, Is>(span.size())])...);
    return 0;
  } else {
    return CastRet(f(CastArg<std::tuple_element_t<Is, ArgsTuple>>(
        span[ArgSlotIndex<ArgsTuple, Is>(span.size())])...));
  }
}

template <typename Tuple, typename F>
uint64_t CallHelper(F&& f, absl::Span<const uint64_t> span) {
  constexpr size_t size = std::tuple_size_v<Tuple>;
  return CallHelperFlat<F, Tuple>(std::forward<F>(f), span,
                                  std::make_index_sequence<size>{});
}

}  // namespace callback_internal

// Callback variables allow the sandboxee to call back to the host.
//
// Note that they come with some restrictions:
// * The number of callbacks that can be active at any time can be limited by
//   the sandbox implementation. See for example `kMaxCallbacks` in the
//   `Sandbox2RPCChannel`.
// * Only integral, enum, pointer, `float`, `double`, and `RemotePtr` types are
//   supported as arguments and return values, with at most 10 arguments. See
//   `RPCChannel::RegisterCallback()` for the limits of each channel.
class Callback : public Var {
 public:
  // Defaults `idempotent` to `true` for backwards compatibility with direct
  // SAPI callers; LWBox-generated wrappers pass `false` unless
  // `SANDBOX_CALLBACK_IDEMPOTENT` is specified.
  //
  // A null function pointer or an empty callable (for example an empty
  // `std::function`) makes an empty callback, which fails to allocate with
  // `FAILED_PRECONDITION`.
  template <typename F>
  explicit Callback(F&& f, bool idempotent = true) {
    using DecayedF = std::decay_t<F>;
    using Traits = callback_internal::FunctionTraits<DecayedF>;
    static_assert(Traits::kArgsCount <= 10,
                  "SAPI callbacks support a maximum of 10 arguments");
    using ArgsTuple = typename Traits::ArgsTuple;
    stack_arg_count_ = callback_internal::StackArgCount<ArgsTuple>();
    if constexpr (std::is_pointer_v<DecayedF>) {
      if (f == nullptr) {
        return;
      }
    } else if constexpr (requires(const DecayedF& fn) {
                           &DecayedF::operator bool;
                         }) {
      if (!static_cast<bool>(f)) {
        return;
      }
    }
    cb_ = [f = std::forward<F>(f), idempotent,
           called = std::make_unique<std::atomic<bool>>(false)](
              absl::Span<const uint64_t> span) mutable -> uint64_t {
      if (!idempotent) {
        CHECK(!called->exchange(true, std::memory_order_acq_rel))
            << "Host callback was invoked more than once without "
               "SANDBOX_CALLBACK_IDEMPOTENT";
      }
      return callback_internal::CallHelper<ArgsTuple>(f, span);
    };
  }

  Callback(const Callback&) = delete;
  Callback& operator=(const Callback&) = delete;

  ~Callback() override {
    if (GetFreeRPCChannel() && GetRemote()) {
      Free(GetFreeRPCChannel()).IgnoreError();
    }
  }

  size_t GetSize() const override { return sizeof(void*); }
  Type GetType() const override { return Type::kPointer; }
  std::string GetTypeString() const override { return "Callback"; }
  std::string ToString() const override { return "Callback"; }

 protected:
  absl::Status Allocate(RPCChannel* rpc_channel, bool automatic_free) override {
    if (GetRemote() != nullptr) {
      return absl::FailedPreconditionError("Callback already allocated");
    }
    if (!cb_) {
      return absl::FailedPreconditionError("Callback functor is empty");
    }
    absl::StatusOr<uintptr_t> remote_ptr =
        stack_arg_count_ == 0 ? rpc_channel->RegisterCallback(std::move(cb_))
                              : rpc_channel->RegisterCallbackWithStackArgs(
                                    std::move(cb_), stack_arg_count_);
    if (!remote_ptr.ok()) {
      return remote_ptr.status();
    }
    SetRemote(reinterpret_cast<void*>(*remote_ptr));
    if (automatic_free) {
      SetFreeRPCChannel(rpc_channel);
    }
    return absl::OkStatus();
  }

  absl::Status Free(RPCChannel* rpc_channel) override {
    if (GetRemote() == nullptr) {
      return absl::OkStatus();
    }
    auto status = rpc_channel->UnregisterCallback(
        reinterpret_cast<uintptr_t>(GetRemote()));
    SetRemote(nullptr);
    return status;
  }

  // Transfer is no-op for callbacks as they are not data.
  absl::Status TransferToSandboxee(RPCChannel* rpc_channel) override {
    return absl::OkStatus();
  }
  absl::Status TransferFromSandboxee(RPCChannel* rpc_channel) override {
    return absl::OkStatus();
  }

 private:
  absl::AnyInvocable<uint64_t(absl::Span<const uint64_t>)> cb_;
  size_t stack_arg_count_ = 0;
};

}  // namespace sapi::v

#endif  // SANDBOXED_API_VAR_CALLBACK_H_
