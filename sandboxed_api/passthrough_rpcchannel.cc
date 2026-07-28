// Copyright 2026 Google LLC
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

#include "sandboxed_api/passthrough_rpcchannel.h"

#include <dlfcn.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/rpcchannel.h"

namespace sapi {

absl::Status PassthroughRPCChannel::Allocate(size_t size, void** addr,
                                             bool disable_shared_memory) {
  // TODO: b/500974875 - Free all memory in the destructor.
  *addr = malloc(size);
  if (!*addr) {
    return absl::ErrnoToStatus(
        errno, "PassthroughRPCChannel::Allocate: malloc failed");
  }
  return absl::OkStatus();
}

absl::Status PassthroughRPCChannel::Reallocate(void* old_addr, size_t size,
                                               void** new_addr) {
  *new_addr = realloc(old_addr, size);
  if (!*new_addr) {
    return absl::ErrnoToStatus(
        errno, "PassthroughRPCChannel::Reallocate: realloc failed");
  }
  return absl::OkStatus();
}

absl::Status PassthroughRPCChannel::Free(void* addr) {
  free(addr);
  return absl::OkStatus();
}

absl::StatusOr<size_t> PassthroughRPCChannel::CopyFromSandbox(
    uintptr_t ptr, absl::Span<char> data) {
  memcpy(data.data(), reinterpret_cast<const char*>(ptr), data.size());
  return data.size();
}

absl::StatusOr<size_t> PassthroughRPCChannel::CopyToSandbox(
    uintptr_t remote_ptr, absl::Span<const char> data) {
  memcpy(reinterpret_cast<char*>(remote_ptr), data.data(), data.size());
  return data.size();
}

absl::Status PassthroughRPCChannel::Symbol(const char* symname, void** addr) {
  FuncRet ret{};
  symbol_function_(symname, &ret);
  if (!ret.success) {
    return absl::InternalError(
        "PassthroughRPCChannel::Symbol: symbol not found");
  }
  *addr = reinterpret_cast<void*>(ret.int_val);
  return absl::OkStatus();
}

absl::Status PassthroughRPCChannel::Exit() {
  return absl::UnimplementedError("PassthroughRPCChannel::Exit");
}

absl::Status PassthroughRPCChannel::SendFD(int local_fd, int* remote_fd) {
  // TODO(sroettger): we should ensure that sandboxee fds are closed when the
  // sandboxee is terminated. But the sandboxee might have closed them already.
  *remote_fd = dup(local_fd);
  if (*remote_fd == -1) {
    return absl::ErrnoToStatus(errno,
                               "PassthroughRPCChannel::SendFD: dup failed");
  }
  return absl::OkStatus();
}

absl::Status PassthroughRPCChannel::RecvFD(int remote_fd, int* local_fd) {
  *local_fd = dup(remote_fd);
  if (*local_fd == -1) {
    return absl::ErrnoToStatus(errno,
                               "PassthroughRPCChannel::RecvFD: dup failed");
  }
  return absl::OkStatus();
}

absl::Status PassthroughRPCChannel::Close(int remote_fd) {
  close(remote_fd);
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<RPCChannel>>
PassthroughRPCChannel::SpawnThread() {
  return absl::UnimplementedError("PassthroughRPCChannel::SpawnThread");
}

absl::StatusOr<size_t> PassthroughRPCChannel::Strlen(void* str) {
  return strlen(reinterpret_cast<const char*>(str));
}

}  // namespace sapi
