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

#ifndef SANDBOXED_API_VAR_REG_H_
#define SANDBOXED_API_VAR_REG_H_

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

#include "absl/strings/str_format.h"
#include "sandboxed_api/var_abstract.h"
#include "sandboxed_api/var_type.h"

namespace sapi::v {

// The superclass for Reg. Specified as a class, so it can be used as a
// type specifier in methods.
using Callable = Var;

// Class Reg represents register-sized variables.
template <typename T>
class Reg : public Callable {
 public:
  static_assert(std::is_integral_v<T> || std::is_floating_point_v<T> ||
                    std::is_pointer_v<T> || std::is_enum_v<T>,
                "Only register-sized types are allowed as template argument "
                "for class Reg.");
  // Store `bool` as `uint8_t` so raw byte writes from untrusted sandboxee
  // returns (`0x02..0xff`) do not produce non-canonical C++ `bool` UB.
  using StorageType =
      std::conditional_t<std::is_same_v<std::remove_cv_t<T>, bool>, uint8_t,
                         std::remove_cv_t<T>>;
  static_assert(sizeof(StorageType) == sizeof(T));

  explicit Reg(const T value = {}) {
    StoreValue(value);
    SetLocal(&value_);
  }

  Reg(Reg<T>&& other) { *this = std::move(other); }

  Reg& operator=(Reg<T>&& other) {
    if (this != &other) {
      Var::operator=(std::move(other));
      std::memcpy(&value_, &other.value_, sizeof(value_));
      SetLocal(&value_);
      other.SetLocal(&other.value_);
    }
    return *this;
  }

  // Getter/Setter for the stored value.
  virtual T GetValue() const {
    if constexpr (std::is_same_v<std::remove_cv_t<T>, bool>) {
      return value_ != 0;
    } else {
      return value_;
    }
  }
  virtual void SetValue(T value) { StoreValue(value); }

  size_t GetSize() const override { return sizeof(T); }

  Type GetType() const override;

  std::string GetTypeString() const override;

  std::string ToString() const override;

 protected:
  // The stored value.
  StorageType value_{};

 private:
  void StoreValue(T value) {
    if constexpr (std::is_same_v<std::remove_cv_t<T>, bool>) {
      value_ = value ? 1 : 0;
    } else if constexpr (std::is_floating_point_v<T>) {
      // Zero trailing padding bytes (e.g. 6-byte padding on x87 `long double`)
      // before storing the value, because the value store writes only the
      // value bytes and `FuncCall` copies the full `sizeof(T)` bytes.
      std::memset(&value_, 0, sizeof(value_));
      value_ = value;
    } else {
      value_ = value;
    }
  }
};

template <typename T>
Type Reg<T>::GetType() const {
  if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
    return Type::kInt;
  }
  if constexpr (std::is_floating_point_v<T>) {
    return Type::kFloat;
  }
  if constexpr (std::is_pointer_v<T>) {
    return Type::kPointer;
  }
  // Not reached
}

template <typename T>
std::string Reg<T>::GetTypeString() const {
  if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
    return "Integer";
  }
  if constexpr (std::is_floating_point_v<T>) {
    return "Floating-point";
  }
  if constexpr (std::is_pointer_v<T>) {
    return "Pointer";
  }
  // Not reached
}

template <typename T>
std::string Reg<T>::ToString() const {
  if constexpr (std::is_same_v<std::remove_cv_t<T>, bool>) {
    return value_ != 0 ? "1" : "0";
  } else if constexpr (std::is_integral_v<T>) {
    return std::to_string(value_);
  }
  if constexpr (std::is_enum_v<T>) {
    return std::to_string(static_cast<std::underlying_type_t<T>>(value_));
  }
  if constexpr (std::is_floating_point_v<T>) {
    return absl::StrFormat("%.10f", value_);
  }
  if constexpr (std::is_pointer<T>::value) {
    return absl::StrFormat("%p", value_);
  }
  // Not reached
}

}  // namespace sapi::v

#endif  // SANDBOXED_API_VAR_REG_H_
