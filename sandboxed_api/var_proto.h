// Copyright 2020 Google LLC. All Rights Reserved.
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

// Provides a class to marshall protobufs in and out of the sandbox

#ifndef SANDBOXED_API_VAR_PROTO_H_
#define SANDBOXED_API_VAR_PROTO_H_

#include <cinttypes>
#include <cstdint>
#include <vector>

#include "absl/base/macros.h"
#include "absl/memory/memory.h"
#include "sandboxed_api/proto_helper.h"
#include "sandboxed_api/var_lenval.h"
#include "sandboxed_api/var_pointable.h"
#include "sandboxed_api/var_ptr.h"
#include "sandboxed_api/util/status_macros.h"

namespace sapi::v {

template <typename T>
class Proto : public Pointable, public Var {
 public:
  static_assert(std::is_base_of<google::protobuf::Message, T>::value,
                "Template argument must be a proto message");

  ABSL_DEPRECATED("Use Proto<>::FromMessage() instead")
  explicit Proto(const T& proto)
      : wrapped_var_(SerializeProto(proto).ValueOrDie()) {}

  static sapi::StatusOr<Proto<T>> FromMessage(const T& proto) {
    SAPI_ASSIGN_OR_RETURN(std::vector<uint8_t> len_val, SerializeProto(proto));
    return Proto(len_val);
  }

  size_t GetSize() const final { return wrapped_var_.GetSize(); }
  Type GetType() const final { return Type::kProto; }
  std::string GetTypeString() const final { return "Protobuf"; }
  std::string ToString() const final { return "Protobuf"; }

  Ptr* CreatePtr(Pointable::SyncType type) override {
    return new Ptr(this, type);
  }

  void* GetRemote() const override { return wrapped_var_.GetRemote(); }
  void* GetLocal() const override { return wrapped_var_.GetLocal(); }

  // Returns a copy of the stored protobuf object.
  sapi::StatusOr<T> GetMessage() const {
    return DeserializeProto<T>(
        reinterpret_cast<const char*>(wrapped_var_.GetData()),
        wrapped_var_.GetDataSize());
  }

  ABSL_DEPRECATED("Use GetMessage() instead")
  std::unique_ptr<T> GetProtoCopy() const {
    auto result_or = GetMessage();
    if (result_or.ok()) {
      return absl::make_unique<T>(std::move(result_or).ValueOrDie());
    }
    return nullptr;
  }

  void SetRemote(void* /* remote */) override {
    // We do not support that much indirection (pointer to a pointer to a
    // protobuf) as it is unlikely that this is wanted behavior. If you expect
    // this to work, please get in touch with us.
    LOG(FATAL) << "SetRemote not supported on protobufs.";
  }

 protected:
  // Forward a couple of function calls to the actual var.
  sapi::Status Allocate(RPCChannel* rpc_channel, bool automatic_free) override {
    return wrapped_var_.Allocate(rpc_channel, automatic_free);
  }

  sapi::Status Free(RPCChannel* rpc_channel) override {
    return sapi::OkStatus();
  }

  sapi::Status TransferToSandboxee(RPCChannel* rpc_channel,
                                   pid_t pid) override {
    return wrapped_var_.TransferToSandboxee(rpc_channel, pid);
  }

  sapi::Status TransferFromSandboxee(RPCChannel* rpc_channel,
                                     pid_t pid) override {
    return wrapped_var_.TransferFromSandboxee(rpc_channel, pid);
  }

 private:
  explicit Proto(std::vector<uint8_t> data) : wrapped_var_(data) {}

  // The management of reading/writing the data to the sandboxee is handled by
  // the LenVal class.
  LenVal wrapped_var_;
};

}  // namespace sapi::v

#endif  // SANDBOXED_API_VAR_PROTO_H_
