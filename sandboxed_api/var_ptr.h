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

#ifndef SANDBOXED_API_VAR_PTR_H_
#define SANDBOXED_API_VAR_PTR_H_

#include <memory>

#include "absl/base/macros.h"
#include "absl/strings/str_format.h"
#include "sandboxed_api/var_abstract.h"
#include "sandboxed_api/var_reg.h"

namespace sapi::v {

// Class representing a pointer. Takes both Var* and regular pointers in the
// initializers.
class Ptr : public Reg<Var*> {
 public:
  Ptr() = delete;

  explicit Ptr(Var* value, SyncType sync_type) : sync_type_(sync_type) {
    Reg<Var*>::SetValue(value);
  }

  Var* GetPointedVar() const { return Reg<Var*>::GetValue(); }

  void SetValue(Var* ptr) final { value_->SetRemote(ptr); }

  Var* GetValue() const final {
    return reinterpret_cast<Var*>(value_->GetRemote());
  }

  const void* GetDataPtr() final {
    remote_ptr_cache_ = GetValue();
    return &remote_ptr_cache_;
  }

  void SetDataFromPtr(const void* ptr, size_t max_sz) final {
    void* tmp;
    memcpy(&tmp, ptr, std::min(sizeof(tmp), max_sz));
    SetValue(reinterpret_cast<Var*>(tmp));
  }

  // Getter/Setter for the sync_type_ field.
  SyncType GetSyncType() { return sync_type_; }
  void SetSyncType(SyncType sync_type) { sync_type_ = sync_type; }

  std::string ToString() const final {
    Var* var = GetPointedVar();
    return absl::StrFormat(
        "Ptr to obj:%p (type:'%s' val:'%s'), local:%p, remote:%p, size:%tx",
        var, var->GetTypeString(), var->ToString(), var->GetLocal(),
        var->GetRemote(), var->GetSize());
  }

 private:
  // GetDataPtr() interface requires of us to return a pointer to the data
  // (variable) that can be copied. We cannot get pointer to pointer with
  // Var::GetRemote(), hence we cache it, and return pointer to it.
  void* remote_ptr_cache_;

  // Shall we synchronize the underlying object before/after call.
  SyncType sync_type_;
};

// Good, old nullptr
class NullPtr : public Ptr {
 public:
  NullPtr() : Ptr(&void_obj_, SyncType::kSyncNone) {}

 private:
  Reg<void*> void_obj_;
};

// Pointer, which can only point to remote memory, and is never synchronized.
class RemotePtr : public Ptr {
 public:
  explicit RemotePtr(void* remote_addr)
      : Ptr(&pointed_obj_, SyncType::kSyncNone) {
    pointed_obj_.SetRemote(remote_addr);
  }

 private:
  Reg<void*> pointed_obj_;
};

}  // namespace sapi::v

#endif  // SANDBOXED_API_VAR_PTR_H_
