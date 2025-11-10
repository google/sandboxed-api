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

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/strings/str_format.h"
#include "sandboxed_api/var_abstract.h"
#include "sandboxed_api/var_reg.h"

namespace sapi::v {

// Class representing a pointer. Takes both Var* and regular pointers in the
// initializers.
class Ptr {
 public:
  enum SyncType {
    // Do not synchronize the underlying object after/before calls.
    kSyncNone = 0x0,
    // Synchronize the underlying object (send the data to the sandboxee)
    // before the call takes place.
    kSyncBefore = 0x1,
    // Synchronize the underlying object (retrieve data from the sandboxee)
    // after the call has finished.
    kSyncAfter = 0x2,
    // Synchronize the underlying object with the remote object, by sending the
    // data to the sandboxee before the call, and retrieving it from the
    // sandboxee after the call has finished.
    kSyncBoth = kSyncBefore | kSyncAfter,
  };

  Ptr() = delete;

  explicit Ptr(Var* value, SyncType sync_type)
      : pointed_var_(value), sync_type_(sync_type) {}

  Var* GetPointedVar() const { return pointed_var_; }

  // Getter/Setter for the sync_type_ field.
  SyncType GetSyncType() { return sync_type_; }
  void SetSyncType(SyncType sync_type) { sync_type_ = sync_type; }

  std::string ToString() const {
    Var* var = pointed_var_;
    return absl::StrFormat(
        "Ptr to obj:%p (type:'%s' val:'%s'), local:%p, remote:%p, size:%tx",
        var, var->GetTypeString(), var->ToString(), var->GetLocal(),
        var->GetRemote(), var->GetSize());
  }

 private:
  Var* pointed_var_;

  // Shall we synchronize the underlying object before/after call.
  SyncType sync_type_;
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
