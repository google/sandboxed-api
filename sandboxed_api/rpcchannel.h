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

#ifndef SANDBOXED_API_RPCCHANNEL_H_
#define SANDBOXED_API_RPCCHANNEL_H_

#include <cstddef>
#include <cstdint>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/var_type.h"

namespace sapi {

// This class exposes functions which provide primitives operating over the
// Comms channel.
class RPCChannel {
 public:
  virtual ~RPCChannel() = default;

  // Calls a function.
  virtual absl::Status Call(const FuncCall& call, uint32_t tag, FuncRet* ret,
                            v::Type exp_type) = 0;

  // Allocates memory.
  virtual absl::Status Allocate(size_t size, void** addr,
                                bool disable_shared_memory = false) = 0;

  // Reallocates memory.
  virtual absl::Status Reallocate(void* old_addr, size_t size,
                                  void** new_addr) = 0;

  // Frees memory.
  virtual absl::Status Free(void* addr) = 0;

  // Reads `data`'s length of bytes from `ptr` in the sandboxee, returns number
  // of bytes read or an error.
  virtual absl::StatusOr<size_t> CopyFromSandbox(uintptr_t ptr,
                                                 absl::Span<char> data) = 0;

  // Writes `data` to `ptr` in the sandboxee, returns number of bytes written or
  // an error.
  virtual absl::StatusOr<size_t> CopyToSandbox(uintptr_t remote_ptr,
                                               absl::Span<const char> data) = 0;

  // Returns address of a symbol.
  virtual absl::Status Symbol(const char* symname, void** addr) = 0;

  // Makes the remote part exit.
  virtual absl::Status Exit() = 0;

  // Transfers fd to sandboxee.
  virtual absl::Status SendFD(int local_fd, int* remote_fd) = 0;

  // Retrieves fd from sandboxee.
  virtual absl::Status RecvFD(int remote_fd, int* local_fd) = 0;

  // Closes fd in sandboxee.
  virtual absl::Status Close(int remote_fd) = 0;

  // Returns length of a null-terminated c-style string (invokes strlen).
  virtual absl::StatusOr<size_t> Strlen(void* str) = 0;

  // Registers a host-side callback function to be called from the sandboxee.
  // Returns the remote pointer to the trampoline that triggers this callback.
  //
  // Limitations:
  // - Supports a maximum of 6 arguments (enforced by CallbackRequest).
  // - Floating-point arguments and return values are NOT supported because the
  //   assembly trampolines only save general-purpose registers and do not
  //   capture floating-point registers (XMM/VFP) used by the ABI for floats.
  // - Supported types are limited to integral, enum, pointer, and
  //   sapi::v::RemotePtr.
  virtual absl::StatusOr<uintptr_t> RegisterCallback(
      absl::AnyInvocable<uint64_t(absl::Span<const uint64_t>)> cb) {
    return absl::UnimplementedError("Callbacks not supported by this channel");
  }

  virtual absl::Status UnregisterCallback(uintptr_t remote_ptr) {
    return absl::UnimplementedError("Callbacks not supported by this channel");
  }
};

}  // namespace sapi

#endif  // SANDBOXED_API_RPCCHANNEL_H_
