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

#include "sandboxed_api/sandbox2_rpcchannel.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/util/status_macros.h"
#include "sandboxed_api/var_type.h"

namespace sapi {

absl::Status Sandbox2RPCChannel::Call(const FuncCall& call, uint32_t tag,
                                      FuncRet* ret, v::Type exp_type) {
  absl::MutexLock lock(mutex_);
  SAPI_ASSIGN_OR_RETURN(*ret, Exchange(tag, &call, sizeof(call), exp_type));
  return absl::OkStatus();
}

absl::StatusOr<FuncRet> Sandbox2RPCChannel::Exchange(uint32_t tag,
                                                     const void* data,
                                                     size_t len,
                                                     v::Type exp_type) {
  uint32_t recv_tag;
  std::vector<uint8_t> recv_value;
  if (!comms_->ExchangeTLV(
          tag, absl::MakeSpan(static_cast<const uint8_t*>(data), len),
          &recv_tag, &recv_value)) {
    return absl::UnavailableError("Exchanging TLV value failed");
  }

  while (recv_tag == comms::kMsgCallback) {
    if (recv_value.size() != sizeof(CallbackRequest)) {
      return absl::UnavailableError(
          "Received incorrect length for CallbackRequest");
    }
    CallbackRequest req;
    memcpy(&req, recv_value.data(), sizeof(CallbackRequest));

    uint64_t ret = 0;
    if (req.index < callbacks_.size() && callbacks_[req.index]) {
      ret = callbacks_[req.index](req.args);
    } else {
      LOG(ERROR) << "Callback index " << req.index << " not registered or null";
    }

    CallbackResponse resp;
    resp.ret = ret;

    if (!comms_->ExchangeTLV(
            comms::kMsgCallbackRet,
            absl::MakeSpan(reinterpret_cast<const uint8_t*>(&resp),
                           sizeof(resp)),
            &recv_tag, &recv_value)) {
      return absl::UnavailableError("Callback response ExchangeTLV failed");
    }
  }

  if (recv_tag != comms::kMsgReturn) {
    LOG(ERROR) << "recv_tag != kMsgReturn (" << recv_tag
               << " != " << comms::kMsgReturn << ")";
    return absl::UnavailableError("Received unexpected tag");
  }
  if (recv_value.size() != sizeof(FuncRet)) {
    LOG(ERROR) << "recv_value.size() != sizeof(FuncRet) (" << recv_value.size()
               << " != " << sizeof(FuncRet) << ")";
    return absl::UnavailableError("Received incorrect length");
  }
  FuncRet ret;
  memcpy(&ret, recv_value.data(), sizeof(FuncRet));
  if (ret.ret_type != exp_type) {
    LOG(ERROR) << "FuncRet->type != exp_type (" << ret.ret_type
               << " != " << exp_type << ")";
    return absl::UnavailableError("Received TLV has incorrect return type");
  }
  if (!ret.success) {
    LOG(ERROR) << "FuncRet->success == false";
    return absl::UnavailableError("Function call failed");
  }
  return ret;
}

absl::StatusOr<FuncRet> Sandbox2RPCChannel::Return(v::Type exp_type) {
  uint32_t tag;
  size_t len;
  FuncRet ret;
  if (!comms_->RecvTLV(&tag, &len, &ret, sizeof(ret), comms::kMsgReturn)) {
    return absl::UnavailableError("Receiving TLV value failed");
  }
  if (len != sizeof(FuncRet)) {
    LOG(ERROR) << "len != sizeof(FuncReturn) (" << len
               << " != " << sizeof(FuncRet) << ")";
    return absl::UnavailableError("Received TLV has incorrect length");
  }
  if (!ret.success) {
    LOG(ERROR) << "FuncRet->success == false";
    return absl::UnavailableError("Function call failed");
  }
  if (ret.ret_type != exp_type) {
    LOG(ERROR) << "FuncRet->type != exp_type (" << ret.ret_type
               << " != " << exp_type << ")";
    return absl::UnavailableError("Received TLV has incorrect return type");
  }
  return ret;
}

absl::Status Sandbox2RPCChannel::Allocate(size_t size, void** addr,
                                          bool disable_shared_memory) {
  absl::MutexLock lock(mutex_);
  SAPI_ASSIGN_OR_RETURN(auto fret, Exchange(comms::kMsgAllocate, &size,
                                            sizeof(size), v::Type::kPointer));
  *addr = reinterpret_cast<void*>(fret.int_val);
  return absl::OkStatus();
}

absl::Status Sandbox2RPCChannel::Reallocate(void* old_addr, size_t size,
                                            void** new_addr) {
  absl::MutexLock lock(mutex_);
  comms::ReallocRequest req = {
      .old_addr = reinterpret_cast<uintptr_t>(old_addr),
      .size = size,
  };

  auto fret =
      Exchange(comms::kMsgReallocate, &req, sizeof(req), v::Type::kPointer);
  if (!fret.ok()) {
    *new_addr = nullptr;
    return absl::UnavailableError(absl::StrCat(
        "Reallocate() failed on the remote side: ", fret.status().message()));
  }
  *new_addr = reinterpret_cast<void*>(fret->int_val);
  return absl::OkStatus();
}

absl::Status Sandbox2RPCChannel::Free(void* addr) {
  absl::MutexLock lock(mutex_);
  uintptr_t remote = reinterpret_cast<uintptr_t>(addr);
  return Exchange(comms::kMsgFree, &remote, sizeof(remote), v::Type::kVoid)
      .status();
}

absl::StatusOr<size_t> Sandbox2RPCChannel::CopyFromSandbox(
    uintptr_t ptr, absl::Span<char> data) {
  return sandbox2::util::ReadBytesFromPidInto(pid_, ptr, data);
}

absl::StatusOr<size_t> Sandbox2RPCChannel::CopyToSandbox(
    uintptr_t remote_ptr, absl::Span<const char> data) {
  SAPI_ASSIGN_OR_RETURN(
      size_t ret, sandbox2::util::WriteBytesToPidFrom(pid_, remote_ptr, data));
  SAPI_RETURN_IF_ERROR(
      MarkMemoryInit(reinterpret_cast<void*>(remote_ptr), data.size()));
  return ret;
}

absl::Status Sandbox2RPCChannel::Symbol(const char* symname, void** addr) {
  absl::MutexLock lock(mutex_);
  SAPI_ASSIGN_OR_RETURN(
      auto fret, Exchange(comms::kMsgSymbol, symname, strlen(symname) + 1,
                          v::Type::kPointer));
  *addr = reinterpret_cast<void*>(fret.int_val);
  return absl::OkStatus();
}

absl::Status Sandbox2RPCChannel::Exit() {
  absl::MutexLock lock(mutex_);
  if (comms_->IsTerminated()) {
    VLOG(2) << "Comms channel already terminated";
    return absl::OkStatus();
  }

  // Try the RPC exit sequence. But, the only thing that matters as a success
  // indicator is whether the Comms channel had been closed
  comms_->SendTLV(comms::kMsgExit, 0, nullptr);
  return absl::OkStatus();
}

absl::Status Sandbox2RPCChannel::SendFD(int local_fd, int* remote_fd) {
  absl::MutexLock lock(mutex_);
  if (!comms_->SendTLV(comms::kMsgSendFd, 0, nullptr)) {
    return absl::UnavailableError("Sending TLV value failed");
  }
  if (!comms_->SendFD(local_fd)) {
    return absl::UnavailableError("Sending FD failed");
  }

  SAPI_ASSIGN_OR_RETURN(auto fret, Return(v::Type::kInt));
  if (!fret.success) {
    return absl::UnavailableError("SendFD failed on the remote side");
  }
  *remote_fd = fret.int_val;
  return absl::OkStatus();
}

absl::Status Sandbox2RPCChannel::RecvFD(int remote_fd, int* local_fd) {
  absl::MutexLock lock(mutex_);
  if (!comms_->SendTLV(comms::kMsgRecvFd, sizeof(remote_fd), &remote_fd)) {
    return absl::UnavailableError("Sending TLV value failed");
  }

  if (!comms_->RecvFD(local_fd)) {
    return absl::UnavailableError("Receving FD failed");
  }

  SAPI_ASSIGN_OR_RETURN(auto fret, Return(v::Type::kVoid));
  if (!fret.success) {
    return absl::UnavailableError("RecvFD failed on the remote side");
  }
  return absl::OkStatus();
}

absl::Status Sandbox2RPCChannel::Close(int remote_fd) {
  absl::MutexLock lock(mutex_);
  SAPI_RETURN_IF_ERROR(
      Exchange(comms::kMsgClose, &remote_fd, sizeof(remote_fd), v::Type::kVoid)
          .status());
  return absl::OkStatus();
}

absl::StatusOr<size_t> Sandbox2RPCChannel::Strlen(void* str) {
  absl::MutexLock lock(mutex_);
  SAPI_ASSIGN_OR_RETURN(
      auto fret, Exchange(comms::kMsgStrlen, &str, sizeof(str), v::Type::kInt));
  return fret.int_val;
}

absl::Status Sandbox2RPCChannel::MarkMemoryInit(void* addr, size_t size) {
  if constexpr (sapi::sanitizers::IsMSan()) {
    absl::MutexLock lock(mutex_);
    comms::ReallocRequest req = {
        .old_addr = reinterpret_cast<uintptr_t>(addr),
        .size = size,
    };
    SAPI_RETURN_IF_ERROR(
        Exchange(comms::kMsgMarkMemoryInit, &req, sizeof(req), v::Type::kVoid)
            .status());
  }
  return absl::OkStatus();
}

absl::StatusOr<uintptr_t> Sandbox2RPCChannel::GetTrampolineTableAddr() {
  if (trampoline_table_addr_ != 0) {
    return trampoline_table_addr_;
  }
  const std::string_view symname = "sapi_callback_trampolines";
  ABSL_ASSIGN_OR_RETURN(auto fret,
                        Exchange(comms::kMsgSymbol, symname.data(),
                                 symname.length() + 1, v::Type::kPointer));
  trampoline_table_addr_ = fret.int_val;
  if (trampoline_table_addr_ == 0) {
    return absl::NotFoundError(
        "sapi_callback_trampolines symbol not found in sandboxee");
  }
  return trampoline_table_addr_;
}

absl::StatusOr<uintptr_t> Sandbox2RPCChannel::RegisterCallback(
    absl::AnyInvocable<uint64_t(absl::Span<const uint64_t>)> cb) {
  absl::MutexLock lock(mutex_);
  ABSL_ASSIGN_OR_RETURN(uintptr_t trampoline_table_addr,
                        GetTrampolineTableAddr());

  auto it = std::find(callbacks_.begin(), callbacks_.end(), nullptr);
  if (it == callbacks_.end()) {
    return absl::ResourceExhaustedError(absl::StrCat(
        "All ", kMaxCallbacks, " callback slots are already registered"));
  }
  *it = std::move(cb);
  size_t slot = std::distance(callbacks_.begin(), it);
  uintptr_t remote_ptr = trampoline_table_addr + slot * kTrampolineSize;
  return remote_ptr;
}

absl::Status Sandbox2RPCChannel::UnregisterCallback(uintptr_t remote_ptr) {
  absl::MutexLock lock(mutex_);
  SAPI_ASSIGN_OR_RETURN(uintptr_t trampoline_table_addr,
                        GetTrampolineTableAddr());
  if (remote_ptr < trampoline_table_addr) {
    return absl::InvalidArgumentError("Invalid remote_ptr");
  }
  size_t offset = remote_ptr - trampoline_table_addr;
  if (offset % kTrampolineSize != 0) {
    return absl::InvalidArgumentError("Invalid remote_ptr alignment");
  }
  size_t slot = offset / kTrampolineSize;
  if (slot >= callbacks_.size()) {
    return absl::InvalidArgumentError("Invalid remote_ptr slot");
  }
  callbacks_[slot] = nullptr;
  return absl::OkStatus();
}

}  // namespace sapi
