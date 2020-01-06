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

#ifndef SANDBOXED_API_VAR_ABSTRACT_H_
#define SANDBOXED_API_VAR_ABSTRACT_H_

#include <memory>
#include <string>
#include <type_traits>

#include "absl/base/macros.h"
#include "sandboxed_api/var_type.h"
#include "sandboxed_api/util/status.h"

namespace sandbox2 {
class Comms;
}  // namespace sandbox2

namespace sapi {
class Sandbox;
class RPCChannel;
}  // namespace sapi

namespace sapi::v {

class Ptr;

// An abstract class representing variables.
class Var {
 public:
  Var(const Var&) = delete;
  Var& operator=(const Var&) = delete;

  // Returns the address of the storage (remote side).
  virtual void* GetRemote() const { return remote_; }

  // Sets the address of the remote storage.
  virtual void SetRemote(void* remote) { remote_ = remote; }

  // Returns the address of the storage (local side).
  virtual void* GetLocal() const { return local_; }

  // Returns the size of the local variable storage.
  virtual size_t GetSize() const = 0;

  // Returns the type of the variable.
  virtual Type GetType() const = 0;

  // Returns a string representation of the variable type.
  virtual std::string GetTypeString() const = 0;

  // Returns a string representation of the variable value.
  virtual std::string ToString() const = 0;

  virtual ~Var();

 protected:
  Var() : local_(nullptr), remote_(nullptr), free_rpc_channel_(nullptr) {}

  // Set pointer to local storage class.
  void SetLocal(void* local) { local_ = local; }

  // Setter/Getter for the address of a Comms object which can be used to
  // remotely free allocated memory backing up this variable, upon this
  // object's end of life-time
  void SetFreeRPCChannel(RPCChannel* rpc_channel) {
    free_rpc_channel_ = rpc_channel;
  }
  RPCChannel* GetFreeRPCChannel() { return free_rpc_channel_; }

  // Allocates the local variable on the remote side. The 'automatic_free'
  // argument dictates whether the remote memory should be freed upon end of
  // this object's lifetime.
  virtual sapi::Status Allocate(RPCChannel* rpc_channel, bool automatic_free);

  // Frees the local variable on the remote side.
  virtual sapi::Status Free(RPCChannel* rpc_channel);

  // Transfers the variable to the sandboxee's address space, has to be
  // allocated there first.
  virtual sapi::Status TransferToSandboxee(RPCChannel* rpc_channel, pid_t pid);

  // Transfers the variable from the sandboxee's address space.
  virtual sapi::Status TransferFromSandboxee(RPCChannel* rpc_channel,
                                             pid_t pid);

 private:
  // Pointer to local storage of the variable.
  void* local_;
  // Pointer to remote storage of the variable.
  void* remote_;

  // Comms which can be used to free resources allocated in the sandboxer upon
  // this process' end of lifetime.
  RPCChannel* free_rpc_channel_;

  // Invokes Allocate()/Free()/Transfer*Sandboxee().
  friend class ::sapi::Sandbox;
};

}  // namespace sapi::v

#endif  // SANDBOXED_API_VAR_ABSTRACT_H_
