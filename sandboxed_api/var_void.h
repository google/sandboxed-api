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

#ifndef SANDBOXED_API_VAR_VOID_H_
#define SANDBOXED_API_VAR_VOID_H_

#include <memory>

#include "sandboxed_api/var_ptr.h"
#include "sandboxed_api/var_reg.h"

namespace sapi::v {

// Good, old void.
class Void : public Callable {
 public:
  Void() = default;

  size_t GetSize() const final { return 0U; }
  Type GetType() const final { return Type::kVoid; }
  std::string GetTypeString() const final { return "Void"; }
  std::string ToString() const final { return "Void"; }

  const void* GetDataPtr() override { return nullptr; }
  void SetDataFromPtr(const void* ptr, size_t max_sz) override {}
};

}  // namespace sapi::v

#endif  // SANDBOXED_API_VAR_VOID_H_
