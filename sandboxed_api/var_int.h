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

#ifndef SANDBOXED_API_VAR_INT_H_
#define SANDBOXED_API_VAR_INT_H_

#include <memory>

#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/var_ptr.h"
#include "sandboxed_api/var_reg.h"

namespace sapi::v {

// Intermediate class for register sized variables so we don't have to implement
// ptr() everywhere.
template <class T>
class IntBase : public Reg<T> {
 public:
  explicit IntBase(T value = {}) { this->SetValue(value); }
};

using Bool = IntBase<bool>;
using Char = IntBase<char>;
using UChar = IntBase<unsigned char>;
using SChar = IntBase<signed char>;

using Short = IntBase<short>;            // NOLINT
using UShort = IntBase<unsigned short>;  // NOLINT
using SShort = IntBase<signed short>;    // NOLINT

using Int = IntBase<int>;
using UInt = IntBase<unsigned int>;
using SInt = IntBase<signed int>;

using Long = IntBase<long>;                  // NOLINT
using ULong = IntBase<unsigned long>;        // NOLINT
using SLong = IntBase<signed long>;          // NOLINT
using LLong = IntBase<long long>;            // NOLINT
using ULLong = IntBase<unsigned long long>;  // NOLINT
using SLLong = IntBase<signed long long>;    // NOLINT

class GenericPtr : public IntBase<uintptr_t> {
 public:
  GenericPtr() { SetValue(0); }
  explicit GenericPtr(uintptr_t val) { SetValue(val); }
  explicit GenericPtr(void* val) { SetValue(reinterpret_cast<uintptr_t>(val)); }
};

class Fd : public Int {
 public:
  Type GetType() const override { return Type::kFd; }
  explicit Fd(int val) { SetValue(val); }
  ~Fd() override;

  // Getter and setter of remote file descriptor.
  void SetRemoteFd(int remote_fd) { remote_fd_ = remote_fd; }
  int GetRemoteFd() { return remote_fd_; }

  // Sets remote and local fd ownership, true by default.
  // Owned fd will be closed during object destruction.
  void OwnRemoteFd(bool owned) { own_remote_ = owned; }
  void OwnLocalFd(bool owned) { own_local_ = owned; }

  // Close remote fd in the sadboxee.
  absl::Status CloseRemoteFd(RPCChannel* rpc_channel);
  // Close local fd.
  void CloseLocalFd();

 protected:
  // Sends local fd to sandboxee, takes ownership of the fd.
  absl::Status TransferFromSandboxee(RPCChannel* rpc_channel,
                                     pid_t pid) override;

  // Retrieves remote file descriptor, does not own fd.
  absl::Status TransferToSandboxee(RPCChannel* rpc_channel, pid_t pid) override;

 private:
  int remote_fd_ = -1;
  bool own_local_ = true;
  bool own_remote_ = true;
};

}  // namespace sapi::v

#endif  // SANDBOXED_API_VAR_INT_H_
