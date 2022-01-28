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

#include <memory>

#include "absl/base/macros.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/var_ptr.h"

namespace sapi::v {

// Class representing a structure.
template <class T>
class Struct : public Var {
 public:
  // Forwarding constructor to initalize the struct_ field.
  template <typename... Args>
  explicit Struct(Args&&... args) : struct_(std::forward<Args>(args)...) {
    SetLocal(&struct_);
  }

  size_t GetSize() const final { return sizeof(T); }
  Type GetType() const final { return Type::kStruct; }
  std::string GetTypeString() const final { return "Structure"; }
  std::string ToString() const final {
    return absl::StrCat("Structure of size: ", sizeof(struct_));
  }

  const T& data() const { return struct_; }
  T* mutable_data() { return &struct_; }

 protected:
  friend class LenVal;

  T struct_;
};

}  // namespace sapi::v

#endif  // SANDBOXED_API_VAR_STRUCT_H_
