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

#include "sandboxed_api/var_lenval.h"

#include <sys/uio.h>

#include "absl/log/log.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/util/status_macros.h"

namespace sapi::v {

absl::Status LenVal::Allocate(RPCChannel* rpc_channel, bool automatic_free) {
  SAPI_RETURN_IF_ERROR(struct_.Allocate(rpc_channel, automatic_free));
  SAPI_RETURN_IF_ERROR(array_.Allocate(rpc_channel, true));

  // Set data pointer.
  struct_.mutable_data()->data = array_.GetRemote();
  return absl::OkStatus();
}

absl::Status LenVal::Free(RPCChannel* rpc_channel) {
  SAPI_RETURN_IF_ERROR(array_.Free(rpc_channel));
  SAPI_RETURN_IF_ERROR(struct_.Free(rpc_channel));
  return absl::OkStatus();
}

absl::Status LenVal::TransferToSandboxee(RPCChannel* rpc_channel, pid_t pid) {
  // Sync the structure and the underlying array.
  SAPI_RETURN_IF_ERROR(struct_.TransferToSandboxee(rpc_channel, pid));
  SAPI_RETURN_IF_ERROR(array_.TransferToSandboxee(rpc_channel, pid));
  return absl::OkStatus();
}

absl::Status LenVal::TransferFromSandboxee(RPCChannel* rpc_channel, pid_t pid) {
  // Sync the structure back.
  SAPI_RETURN_IF_ERROR(struct_.TransferFromSandboxee(rpc_channel, pid));

  // Resize the local array if required. Also make sure we own the buffer, this
  // is the only way we can be sure that the buffer is writable.
  size_t new_size = struct_.data().size;
  SAPI_RETURN_IF_ERROR(array_.EnsureOwnedLocalBuffer(new_size));

  // Remote pointer might have changed, update it.
  array_.SetRemote(struct_.data().data);
  return array_.TransferFromSandboxee(rpc_channel, pid);
}

absl::Status LenVal::ResizeData(RPCChannel* rpc_channel, size_t size) {
  SAPI_RETURN_IF_ERROR(array_.Resize(rpc_channel, size));
  auto* struct_data = struct_.mutable_data();
  struct_data->data = array_.GetRemote();
  struct_data->size = size;
  return absl::OkStatus();
}

}  // namespace sapi::v
