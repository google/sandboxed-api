// Copyright 2019 Google LLC. All Rights Reserved.
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

#ifndef SANDBOXED_API_RPCCHANNEL_H_
#define SANDBOXED_API_RPCCHANNEL_H_

#include <cstddef>

#include "absl/synchronization/mutex.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/var_type.h"
#include "sandboxed_api/util/status.h"
#include "sandboxed_api/util/statusor.h"

namespace sapi {

// This class exposes functions which provide primitives operating over the
// Comms channel.
class RPCChannel {
 public:
  explicit RPCChannel(sandbox2::Comms* comms) : comms_(comms) {}

  // Calls a function.
  ::sapi::Status Call(const FuncCall& call, uint32_t tag, FuncRet* ret,
                      v::Type exp_type);

  // Allocates memory.
  ::sapi::Status Allocate(size_t size, void** addr);

  // Reallocates memory.
  ::sapi::Status Reallocate(void* old_addr, size_t size, void** new_addr);

  // Frees memory.
  ::sapi::Status Free(void* addr);

  // Returns address of a symbol.
  ::sapi::Status Symbol(const char* symname, void** addr);

  // Makes the remote part exit.
  ::sapi::Status Exit();

  // Transfers fd to sandboxee.
  ::sapi::Status SendFD(int local_fd, int* remote_fd);

  // Retrieves fd from sandboxee.
  ::sapi::Status RecvFD(int remote_fd, int* local_fd);

  // Closes fd in sandboxee.
  ::sapi::Status Close(int remote_fd);

  sandbox2::Comms* comms() const { return comms_; }

 private:
  // Receives the result after a call.
  ::sapi::StatusOr<FuncRet> Return(v::Type exp_type);

  sandbox2::Comms* comms_;  // Owned by sandbox2;
  absl::Mutex mutex_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_RPCCHANNEL_H_
