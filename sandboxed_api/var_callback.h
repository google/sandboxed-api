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

#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/types/span.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/var_abstract.h"
#include "sandboxed_api/var_type.h"

namespace sapi::v {

class RemotePtr;

namespace callback_internal {

template <typename T>
T CastArg(uint64_t val) {
  using DecayedT = std::decay_t<T>;
  static_assert(std::is_integral_v<DecayedT> || std::is_pointer_v<DecayedT> ||
                    std::is_same_v<DecayedT, RemotePtr>,
                "Callback arguments must be integral or pointer types");
  if constexpr (std::is_pointer_v<T>) {
    return reinterpret_cast<T>(val);
  } else {
    return static_cast<T>(val);
  }
}

template <typename T>
uint64_t CastRet(T val) {
  using DecayedT = std::decay_t<T>;
  static_assert(std::is_integral_v<DecayedT> || std::is_pointer_v<DecayedT> ||
                    std::is_same_v<DecayedT, RemotePtr>,
                "Callback return values must be integral or pointer types");
  if constexpr (std::is_pointer_v<T>) {
    return reinterpret_cast<uint64_t>(val);
  } else {
    return static_cast<uint64_t>(val);
  }
}

template <typename T>
struct FunctionTraits;

template <typename R, typename... Args>
struct FunctionTraits<R (*)(Args...)> {
  using ArgsTuple = std::tuple<Args...>;
  static constexpr size_t kArgsCount = sizeof...(Args);
};

template <typename C, typename R, typename... Args>
struct FunctionTraits<R (C::*)(Args...) const> {
  using ArgsTuple = std::tuple<Args...>;
  static constexpr size_t kArgsCount = sizeof...(Args);
};

template <typename C, typename R, typename... Args>
struct FunctionTraits<R (C::*)(Args...)> {
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
  if constexpr (std::is_void_v<R>) {
    f(CastArg<std::tuple_element_t<Is, ArgsTuple>>(span[Is])...);
    return 0;
  } else {
    return CastRet(
        f(CastArg<std::tuple_element_t<Is, ArgsTuple>>(span[Is])...));
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
// * Only primitive types and `RemotePtr` are supported as arguments and return
//   values.
// * TODO(sroettger): We don't support nested calls into the sandbox from the
//                    callback yet.
class Callback : public Var {
 public:
  template <typename F>
  explicit Callback(F&& f) {
    using Traits = callback_internal::FunctionTraits<std::decay_t<F>>;
    static_assert(Traits::kArgsCount <= 6,
                  "SAPI callbacks support a maximum of 6 arguments");
    cb_ =
        [f = std::forward<F>(f)](absl::Span<const uint64_t> span) -> uint64_t {
      using ArgsTuple = typename Traits::ArgsTuple;
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
    ABSL_ASSIGN_OR_RETURN(uintptr_t remote_ptr,
                          rpc_channel->RegisterCallback(std::move(cb_)));
    SetRemote(reinterpret_cast<void*>(remote_ptr));
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
};

}  // namespace sapi::v

#endif  // SANDBOXED_API_VAR_CALLBACK_H_
