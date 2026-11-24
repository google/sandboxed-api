// Copyright 2019 Google LLC
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

#ifndef SANDBOXED_API_VAR_STRUCT_H_
#define SANDBOXED_API_VAR_STRUCT_H_

#include <cstddef>
#include <cstring>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/var_abstract.h"
#include "sandboxed_api/var_type.h"

namespace sapi::v {

// Class representing a structure.
template <class T>
class Struct : public Var {
 public:
  using ValueType = std::remove_cv_t<T>;
  static_assert(std::is_trivially_copyable_v<ValueType>,
                "sapi::v::Struct requires a trivially copyable type");

  // Forwarding constructor to initialize the backing storage after zero-filling
  // so uninitialized members or padding bytes do not leak to the sandboxee.
  template <typename... Args,
            typename = std::enable_if_t<
                !(sizeof...(Args) == 1 &&
                  (std::is_same_v<std::decay_t<Args>, Struct> && ...))>>
  explicit Struct(Args&&... args) {
    new (raw_) ValueType(std::forward<Args>(args)...);
    ClearPadding();
    SetLocal(raw_);
  }

  Struct(Struct&& other) { *this = std::move(other); }

  Struct& operator=(Struct&& other) {
    if (this != &other) {
      Var::operator=(std::move(other));
      std::memcpy(raw_, other.raw_, sizeof(ValueType));
      ClearPadding();
      SetLocal(raw_);
      other.SetLocal(other.raw_);
    }
    return *this;
  }

  size_t GetSize() const final { return sizeof(T); }
  Type GetType() const final { return Type::kStruct; }
  std::string GetTypeString() const final { return "Structure"; }
  std::string ToString() const final {
    return absl::StrCat("Structure of size: ", sizeof(ValueType));
  }

  const T& data() const {
    return *std::launder(reinterpret_cast<const T*>(raw_));
  }
  T* mutable_data() { return std::launder(reinterpret_cast<T*>(raw_)); }

 protected:
  friend class LenVal;

  absl::Status TransferToSandboxee(RPCChannel* rpc_channel) override {
    ClearPadding();
    return Var::TransferToSandboxee(rpc_channel);
  }

  alignas(ValueType) unsigned char raw_[sizeof(ValueType)] = {};

 private:
  void ClearPadding() {
#if defined(__has_builtin)
#if __has_builtin(__builtin_clear_padding)
    if constexpr (!std::is_union_v<ValueType>) {
      __builtin_clear_padding(std::launder(reinterpret_cast<ValueType*>(raw_)));
    }
#endif
#endif
  }
};

}  // namespace sapi::v

#endif  // SANDBOXED_API_VAR_STRUCT_H_
