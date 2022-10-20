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

#include "sandboxed_api/var_int.h"

#include "absl/log/log.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/util/status_macros.h"

namespace sapi::v {

Fd::~Fd() {
  if (GetFreeRPCChannel() && GetRemoteFd() >= 0 && own_remote_) {
    this->CloseRemoteFd(GetFreeRPCChannel()).IgnoreError();
  }
  if (GetValue() >= 0 && own_local_) {
    CloseLocalFd();
  }
}

absl::Status Fd::CloseRemoteFd(RPCChannel* rpc_channel) {
  SAPI_RETURN_IF_ERROR(rpc_channel->Close(GetRemoteFd()));

  SetRemoteFd(-1);
  return absl::OkStatus();
}

void Fd::CloseLocalFd() {
  if (GetValue() < 0) {
    return;
  }
  if (close(GetValue()) != 0) {
    PLOG(WARNING) << "close(" << GetValue() << ") failed";
  }

  SetValue(-1);
}

absl::Status Fd::TransferToSandboxee(RPCChannel* rpc_channel, pid_t /* pid */) {
  int remote_fd;

  SetFreeRPCChannel(rpc_channel);
  OwnRemoteFd(true);

  if (GetValue() < 0) {
    return absl::FailedPreconditionError(
        "Cannot transfer FD: Local FD not valid");
  }

  if (GetRemoteFd() >= 0) {
    return absl::FailedPreconditionError(
        "Cannot transfer FD: Sandboxee already has a valid FD");
  }

  SAPI_RETURN_IF_ERROR(rpc_channel->SendFD(GetValue(), &remote_fd));
  SetRemoteFd(remote_fd);

  return absl::OkStatus();
}

absl::Status Fd::TransferFromSandboxee(RPCChannel* rpc_channel,
                                       pid_t /* pid */) {
  int local_fd;

  SetFreeRPCChannel(rpc_channel);
  OwnRemoteFd(false);

  if (GetValue()) {
    return absl::FailedPreconditionError(
        "Cannot transfer FD back: Our FD is already valid");
  }

  if (GetRemoteFd() < 0) {
    return absl::FailedPreconditionError(
        "Cannot transfer FD back: Sandboxee has no valid FD");
  }

  SAPI_RETURN_IF_ERROR(rpc_channel->RecvFD(GetRemoteFd(), &local_fd));
  SetValue(local_fd);

  return absl::OkStatus();
}

}  // namespace sapi::v
