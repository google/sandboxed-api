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

#ifndef SANDBOXED_API_LWBOX_RUNTIME_SANDBOXEE_REGISTRY_H_
#define SANDBOXED_API_LWBOX_RUNTIME_SANDBOXEE_REGISTRY_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <type_traits>

#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/var_type.h"

namespace sapi::lwbox {

// Arguments of a call to a sandboxee function.
//
// Like `::sapi::FunctionCallPreparer`, but rejects protobuf arguments, which
// lightweight sandboxes never pass. This keeps protobuf out of merged
// sandboxees.
class SandboxeeCallArgs {
 public:
  explicit SandboxeeCallArgs(const ::sapi::FuncCall& call) : call_(call) {
    CHECK(call.argc <= ::sapi::FuncCall::kArgsMax)
        << "Number of arguments of a sandbox call exceeds limits.";
  }

  size_t arg_count() const { return call_.argc; }

  template <typename T>
  T GetArg(size_t idx) const {
    CHECK_LT(idx, call_.argc);
    if constexpr (std::is_same_v<T, bool>) {
      uint8_t raw;
      std::memcpy(&raw, &call_.args[idx].arg_int, sizeof(raw));
      return raw != 0;
    } else if constexpr (std::is_floating_point_v<T>) {
      static_assert(sizeof(T) <= sizeof(call_.args[0].arg_float));
      T ret;
      std::memcpy(&ret, &call_.args[idx].arg_float, sizeof(T));
      return ret;
    } else if constexpr (std::is_pointer_v<T> || std::is_integral_v<T> ||
                         std::is_enum_v<T>) {
      static_assert(sizeof(T) <= sizeof(call_.args[0].arg_int));
      T ret;
      std::memcpy(&ret, &call_.args[idx].arg_int, sizeof(T));
      return ret;
    } else {
      static_assert(std::is_pointer_v<T> || std::is_integral_v<T> ||
                        std::is_enum_v<T> || std::is_floating_point_v<T>,
                    "Unsupported argument type");
    }
  }

  template <typename T>
  bool HasCompatibleArg(size_t idx) const {
    CHECK_LT(idx, call_.argc);
    const ::sapi::v::Type type = call_.arg_type[idx];
    if constexpr (std::is_pointer_v<T>) {
      // A protobuf argument points to a `LenValStruct`, not to a `T`.
      return type == ::sapi::v::Type::kPointer &&
             call_.aux_type[idx] != ::sapi::v::Type::kProto;
    } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
      return type == ::sapi::v::Type::kInt || type == ::sapi::v::Type::kFd;
    } else if constexpr (std::is_floating_point_v<T>) {
      return type == ::sapi::v::Type::kFloat;
    } else {
      static_assert(std::is_pointer_v<T> || std::is_integral_v<T> ||
                        std::is_enum_v<T> || std::is_floating_point_v<T>,
                    "Unsupported argument type");
    }
  }

 private:
  const ::sapi::FuncCall& call_;
};

using SandboxeeFunctionHandler =
    ::sapi::FuncRet (*)(const SandboxeeCallArgs& call);

struct SandboxeeFunctionEntry {
  absl::string_view name;
  SandboxeeFunctionHandler handler;
};

struct SandboxeeSymbolEntry {
  absl::string_view name;
  void* address;
};

// Registers a sandboxed library's generated function handlers and exported
// symbols with the merged LWBox sandboxee dispatcher.
//
// Not thread-safe: must only be called during static initialization before
// `main()` starts serving RPC requests, as the underlying dispatch maps are
// read without synchronization by concurrent RPC worker threads.
void RegisterSandboxeeFunctions(
    std::initializer_list<SandboxeeFunctionEntry> functions,
    std::initializer_list<SandboxeeSymbolEntry> symbols);

}  // namespace sapi::lwbox

#endif  // SANDBOXED_API_LWBOX_RUNTIME_SANDBOXEE_REGISTRY_H_
