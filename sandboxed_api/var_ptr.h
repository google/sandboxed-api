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

#include <cstdint>
#include <string>

#include "absl/log/die_if_null.h"
#include "absl/strings/str_format.h"
#include "sandboxed_api/var_abstract.h"

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

  explicit Ptr(Var* value, SyncType sync_type)
      : pointed_var_(ABSL_DIE_IF_NULL(value)), sync_type_(sync_type) {}
  virtual ~Ptr() = default;

  Ptr(const Ptr&) = default;
  Ptr& operator=(const Ptr&) = default;

  Var* GetPointedVar() const { return pointed_var_; }
  virtual uintptr_t GetRemoteValue() const {
    return reinterpret_cast<uintptr_t>(pointed_var_->GetRemote());
  }

  // Getter/Setter for the sync_type_ field.
  SyncType GetSyncType() { return sync_type_; }

  std::string ToString() const {
    if (pointed_var_ == nullptr) {
      return absl::StrFormat("RemotePtr @ 0x%x", GetRemoteValue());
    }
    Var* var = pointed_var_;
    return absl::StrFormat(
        "Ptr to obj:%p (type:'%s' val:'%s'), local:%p, remote:%p, size:%tx",
        var, var->GetTypeString(), var->ToString(), var->GetLocal(),
        var->GetRemote(), var->GetSize());
  }

 protected:
  struct RemotePtrTag {};
  explicit Ptr(RemotePtrTag) : pointed_var_(nullptr), sync_type_(kSyncNone) {}

 private:
  Var* pointed_var_;

  // Shall we synchronize the underlying object before/after call.
  SyncType sync_type_;
};

// Pointer, which can only point to remote memory, and is never synchronized.
class RemotePtr : public Ptr {
 public:
  explicit RemotePtr(const void* remote_addr)
      : RemotePtr(reinterpret_cast<uintptr_t>(remote_addr)) {}
  explicit RemotePtr(uintptr_t remote_addr)
      : Ptr(RemotePtrTag()), remote_addr_(remote_addr) {}
  RemotePtr(const RemotePtr& other) = default;
  RemotePtr& operator=(const RemotePtr& other) = default;

  uintptr_t GetRemoteValue() const override { return remote_addr_; }

 private:
  uintptr_t remote_addr_;
};

}  // namespace sapi::v

#endif  // SANDBOXED_API_VAR_PTR_H_
