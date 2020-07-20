// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SANDBOXED_API_VAR_REG_H_
#define SANDBOXED_API_VAR_REG_H_

#include <inttypes.h>
#include <iostream>

#include <glog/logging.h>
#include "sandboxed_api/var_abstract.h"

namespace sapi::v {

// The super-class for Reg. Specified as a class, so it can be used as a
// type specifier in methods.
class Callable : public Var {
 public:
  Callable(const Callable&) = delete;
  Callable& operator=(const Callable&) = delete;

  // Get pointer to the stored data.
  virtual const void* GetDataPtr() = 0;
  // Set internal data from ptr.
  virtual void SetDataFromPtr(const void* ptr, size_t max_sz) = 0;
  // Get data from inernal ptr.
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
  static_assert(std::is_integral<T>() || std::is_floating_point<T>() ||
                    std::is_pointer<T>() || std::is_enum<T>(),
                "Only register-sized types are allowed as template argument "
                "for class Reg.");

  Reg() : Reg(static_cast<T>(0)) {}

  explicit Reg(const T val) {
    val_ = val;
    SetLocal(&val_);
  }

  // Getter/Setter for the stored value.
  virtual T GetValue() const { return val_; }
  virtual void SetValue(T val) { val_ = val; }

  const void* GetDataPtr() override {
    return reinterpret_cast<const void*>(&val_);
  }
  void SetDataFromPtr(const void* ptr, size_t max_sz) override {
    memcpy(&val_, ptr, std::min(GetSize(), max_sz));
  }

  size_t GetSize() const override { return sizeof(T); }

  Type GetType() const override {
    if constexpr (std::is_integral<T>() || std::is_enum<T>()) {
      return Type::kInt;
    }
    if constexpr (std::is_floating_point<T>()) {
      return Type::kFloat;
    }
    if constexpr (std::is_pointer<T>()) {
      return Type::kPointer;
    }
    // Not reached
  }

  std::string GetTypeString() const override {
    if constexpr (std::is_integral<T>() || std::is_enum<T>()) {
      return "Integer";
    }
    if constexpr (std::is_floating_point<T>()) {
      return "Floating-point";
    }
    if constexpr (std::is_pointer<T>()) {
      return "Pointer";
    }
    // Not reached
  }

  std::string ToString() const override { return ValToString(); }

 protected:
  // The stored value.
  T val_;

 private:
  std::string ValToString() const {
    char buf[32];
    bool signd = true;
    if (std::is_integral<T>::value || std::is_enum<T>::value) {
      if (std::is_integral<T>::value) {
        signd = std::is_signed<T>::value;
      }

      switch (sizeof(T)) {
        case 1:
          if (signd) {
            snprintf(buf, sizeof(buf), "%" PRId8,
                     *(reinterpret_cast<const uint8_t*>(&val_)));
          } else {
            snprintf(buf, sizeof(buf), "%" PRIu8,
                     *(reinterpret_cast<const uint8_t*>(&val_)));
          }
          break;
        case 2:
          if (signd) {
            snprintf(buf, sizeof(buf), "%" PRId16,
                     *(reinterpret_cast<const uint16_t*>(&val_)));
          } else {
            snprintf(buf, sizeof(buf), "%" PRIu16,
                     *(reinterpret_cast<const uint16_t*>(&val_)));
          }
          break;
        case 4:
          if (signd) {
            snprintf(buf, sizeof(buf), "%" PRId32,
                     *(reinterpret_cast<const uint32_t*>(&val_)));
          } else {
            snprintf(buf, sizeof(buf), "%" PRIu32,
                     *(reinterpret_cast<const uint32_t*>(&val_)));
          }
          break;
        case 8:
          if (signd) {
            snprintf(buf, sizeof(buf), "%" PRId64,
                     *(reinterpret_cast<const uint64_t*>(&val_)));
          } else {
            snprintf(buf, sizeof(buf), "%" PRIu64,
                     *(reinterpret_cast<const uint64_t*>(&val_)));
          }
          break;
        default:
          LOG(FATAL) << "Incorrect type";
          break;
      }
    } else if (std::is_floating_point<T>::value) {
      if (std::is_same<T, float>::value) {
        snprintf(buf, sizeof(buf), "%.10f",
                 *(reinterpret_cast<const float*>(&val_)));
      } else if (std::is_same<T, double>::value) {
        snprintf(buf, sizeof(buf), "%.10lf",
                 *(reinterpret_cast<const double*>(&val_)));
      } else if (std::is_same<T, long double>::value) {
        snprintf(buf, sizeof(buf), "%.10Lf",
                 *(reinterpret_cast<const long double*>(&val_)));
      }
    } else if (std::is_pointer<T>::value) {
      snprintf(buf, sizeof(buf), "%p", *reinterpret_cast<void* const*>(&val_));
    } else {
      LOG(FATAL) << "Incorrect type";
    }

    return std::string(buf);
  }
};

}  // namespace sapi::v

#endif  // SANDBOXED_API_VAR_REG_H_
