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

#include <iostream>
#include <string>
#include <type_traits>

#include "absl/strings/str_format.h"
#include "sandboxed_api/var_abstract.h"

namespace sapi::v {

// The super-class for Reg. Specified as a class, so it can be used as a
// type specifier in methods.
class Callable : public Var {
 public:
  // Get pointer to the stored data.
  virtual const void* GetDataPtr() = 0;

  // Set internal data from ptr.
  virtual void SetDataFromPtr(const void* ptr, size_t max_sz) = 0;

  // Get data from internal ptr.
  void GetDataFromPtr(void* ptr, size_t max_sz) {
    size_t min_sz = std::min<size_t>(GetSize(), max_sz);
    memcpy(ptr, GetDataPtr(), min_sz);
  }

 protected:
  Callable() = default;
};

// class Reg represents register-sized variables.
template <typename T>
class Reg : public Callable {
 public:
  static_assert(std::is_integral_v<T> || std::is_floating_point_v<T> ||
                    std::is_pointer_v<T> || std::is_enum_v<T>,
                "Only register-sized types are allowed as template argument "
                "for class Reg.");

  explicit Reg(const T value = {}) {
    value_ = value;
    SetLocal(&value_);
  }

  // Getter/Setter for the stored value.
  virtual T GetValue() const { return value_; }
  virtual void SetValue(T value) { value_ = value; }

  const void* GetDataPtr() override {
    return reinterpret_cast<const void*>(&value_);
  }
  void SetDataFromPtr(const void* ptr, size_t max_sz) override {
    memcpy(&value_, ptr, std::min(GetSize(), max_sz));
  }

  size_t GetSize() const override { return sizeof(T); }

  Type GetType() const override;

  std::string GetTypeString() const override;

  std::string ToString() const override;

 protected:
  // The stored value.
  T value_;
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
  if constexpr (std::is_integral_v<T>) {
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
  // Not reached.
}

}  // namespace sapi::v

#endif  // SANDBOXED_API_VAR_REG_H_
