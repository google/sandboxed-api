// Copyright 2025 Google LLC
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

#ifndef SANDBOXED_API_SANDBOX2_RPCCHANNEL_H_
#define SANDBOXED_API_SANDBOX2_RPCCHANNEL_H_

#include <cstddef>
#include <cstdint>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/var_type.h"

namespace sandbox2 {
class Comms;
}  // namespace sandbox2

namespace sapi {

// This class exposes functions which provide primitives operating over the
// Comms channel.
class Sandbox2RPCChannel : public RPCChannel {
 public:
  explicit Sandbox2RPCChannel(sandbox2::Comms* comms, pid_t pid)
      : comms_(comms), pid_(pid) {}

  // Calls a function.
  absl::Status Call(const FuncCall& call, uint32_t tag, FuncRet* ret,
                    v::Type exp_type) override;

  // Allocates memory.
  absl::Status Allocate(size_t size, void** addr,
                        bool disable_shared_memory = false) override;

  // Reallocates memory.
  absl::Status Reallocate(void* old_addr, size_t size,
                          void** new_addr) override;

  // Frees memory.
  absl::Status Free(void* addr) override;

  // Reads `data`'s length of bytes from `ptr` in the sandboxee, returns number
  // of bytes read or an error.
  absl::StatusOr<size_t> CopyFromSandbox(uintptr_t ptr,
                                         absl::Span<char> data) override;

  // Writes `data` to `ptr` in the sandboxee, returns number of bytes written or
  // an error.
  absl::StatusOr<size_t> CopyToSandbox(uintptr_t remote_ptr,
                                       absl::Span<const char> data) override;

  // Returns address of a symbol.
  absl::Status Symbol(const char* symname, void** addr) override;

  // Makes the remote part exit.
  absl::Status Exit() override;

  // Transfers fd to sandboxee.
  absl::Status SendFD(int local_fd, int* remote_fd) override;

  // Retrieves fd from sandboxee.
  absl::Status RecvFD(int remote_fd, int* local_fd) override;

  // Closes fd in sandboxee.
  absl::Status Close(int remote_fd) override;

  // Returns length of a null-terminated c-style string (invokes strlen).
  absl::StatusOr<size_t> Strlen(void* str) override;

  sandbox2::Comms* comms() const { return comms_; }

 private:
  // Marks the memory as initialized (used with MSAN).
  absl::Status MarkMemoryInit(void* addr, size_t size);

  // Receives the result after a call.
  absl::StatusOr<FuncRet> Return(v::Type exp_type);

  sandbox2::Comms* comms_;  // Owned by sandbox2;
  // The pid of the sandboxee.
  pid_t pid_;
  absl::Mutex mutex_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_SANDBOX2_RPCCHANNEL_H_
