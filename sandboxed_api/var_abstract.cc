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

// Implementation of sapi::v::Var

#include "sandboxed_api/var_abstract.h"

#include <sys/uio.h>

#include <memory>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/status_macros.h"
#include "sandboxed_api/var_ptr.h"

namespace sapi::v {

Var::~Var() {
  if (free_rpc_channel_ && GetRemote()) {
    this->Free(free_rpc_channel_).IgnoreError();
  }
}

void Var::PtrDeleter::operator()(Ptr* p) { delete p; }

Ptr* Var::PtrNone() {
  if (!ptr_none_) {
    ptr_none_.reset(new Ptr(this, kSyncNone));
  }
  return ptr_none_.get();
}

Ptr* Var::PtrBoth() {
  if (!ptr_both_) {
    ptr_both_.reset(new Ptr(this, kSyncBoth));
  }
  return ptr_both_.get();
}

Ptr* Var::PtrBefore() {
  if (!ptr_before_) {
    ptr_before_.reset(new Ptr(this, kSyncBefore));
  }
  return ptr_before_.get();
}

Ptr* Var::PtrAfter() {
  if (!ptr_after_) {
    ptr_after_.reset(new Ptr(this, kSyncAfter));
  }
  return ptr_after_.get();
}

absl::Status Var::Allocate(RPCChannel* rpc_channel, bool automatic_free) {
  void* addr;
  SAPI_RETURN_IF_ERROR(rpc_channel->Allocate(GetSize(), &addr));

  if (!addr) {
    LOG(ERROR) << "Allocate: returned nullptr";
    return absl::UnavailableError("Allocating memory failed");
  }

  SetRemote(addr);
  if (automatic_free) {
    SetFreeRPCChannel(rpc_channel);
  }

  return absl::OkStatus();
}

absl::Status Var::Free(RPCChannel* rpc_channel) {
  SAPI_RETURN_IF_ERROR(rpc_channel->Free(GetRemote()));

  SetRemote(nullptr);
  return absl::OkStatus();
}

absl::Status Var::TransferToSandboxee(RPCChannel* rpc_channel, pid_t pid) {
  VLOG(3) << "TransferToSandboxee for: " << ToString()
          << ", local: " << GetLocal() << ", remote: " << GetRemote()
          << ", size: " << GetSize();

  if (remote_ == nullptr) {
    LOG(WARNING) << "Object: " << GetType() << " has no remote object set";
    return absl::FailedPreconditionError(
        absl::StrCat("Object: ", GetType(), " has no remote object set"));
  }

  struct iovec local = {
      .iov_base = GetLocal(),
      .iov_len = GetSize(),
  };
  struct iovec remote = {
      .iov_base = GetRemote(),
      .iov_len = GetSize(),
  };

  ssize_t ret = process_vm_writev(pid, &local, 1, &remote, 1, 0);
  if (ret == -1) {
    PLOG(WARNING) << "process_vm_writev(pid: " << pid
                  << " laddr: " << GetLocal() << " raddr: " << GetRemote()
                  << " size: " << GetSize() << ")";
    return absl::UnavailableError("process_vm_writev failed");
  }
  if (ret != GetSize()) {
    LOG(WARNING) << "process_vm_writev(pid: " << pid << " laddr: " << GetLocal()
                 << " raddr: " << GetRemote() << " size: " << GetSize() << ")"
                 << " transferred " << ret << " bytes";
    return absl::UnavailableError("process_vm_writev: partial success");
  }

  return absl::OkStatus();
}

absl::Status Var::TransferFromSandboxee(RPCChannel* rpc_channel, pid_t pid) {
  VLOG(3) << "TransferFromSandboxee for: " << ToString()
          << ", local: " << GetLocal() << ", remote: " << GetRemote()
          << ", size: " << GetSize();

  if (local_ == nullptr) {
    return absl::FailedPreconditionError(
        absl::StrCat("Object: ", GetType(), " has no local storage set"));
  }

  struct iovec local = {
      .iov_base = GetLocal(),
      .iov_len = GetSize(),
  };
  struct iovec remote = {
      .iov_base = GetRemote(),
      .iov_len = GetSize(),
  };

  ssize_t ret = process_vm_readv(pid, &local, 1, &remote, 1, 0);
  if (ret == -1) {
    PLOG(WARNING) << "process_vm_readv(pid: " << pid << " laddr: " << GetLocal()
                  << " raddr: " << GetRemote() << " size: " << GetSize() << ")";
    return absl::UnavailableError("process_vm_readv failed");
  }
  if (ret != GetSize()) {
    LOG(WARNING) << "process_vm_readv(pid: " << pid << " laddr: " << GetLocal()
                 << " raddr: " << GetRemote() << " size: " << GetSize() << ")"
                 << " transferred " << ret << " bytes";
    return absl::UnavailableError("process_vm_readv succeeded partially");
  }

  return absl::OkStatus();
}

}  // namespace sapi::v
