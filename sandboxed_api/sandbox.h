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

#ifndef SANDBOXED_API_SANDBOX_H_
#define SANDBOXED_API_SANDBOX_H_

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/notify.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2_backend.h"
#include "sandboxed_api/sandbox_config.h"
#include "sandboxed_api/var_abstract.h"
#include "sandboxed_api/var_reg.h"
#include "sandboxed_api/vars.h"

namespace sapi {
namespace internal {
class PtrOrCallable {
 public:
  explicit PtrOrCallable(v::Callable* callable) : callable_(callable) {}
  explicit PtrOrCallable(v::Ptr* ptr) : ptr_(ptr) {}
  explicit PtrOrCallable(nullptr_t) : ptr_(nullptr) {}

  bool IsCallable() const { return callable_ != nullptr; }
  bool IsPtr() const { return !IsCallable(); }

  v::Callable* callable() const { return callable_; }
  v::Ptr* ptr() const { return ptr_; }

 private:
  v::Callable* callable_ = nullptr;
  v::Ptr* ptr_ = nullptr;
};
}  // namespace internal

// The Sandbox class represents the sandboxed library. It provides users with
// means to communicate with it (make function calls, transfer memory).
class SandboxBase {
 public:
  SandboxBase() = default;

  virtual ~SandboxBase() = default;

  // Initializes a new sandboxing session.
  virtual absl::Status Init() = 0;

  // Returns whether the current sandboxing session is active.
  virtual bool is_active() const = 0;

  // Terminates the current sandboxing session (if it exists).
  virtual void Terminate(bool attempt_graceful_exit = true) = 0;

  // Restarts the sandbox.
  absl::Status Restart(bool attempt_graceful_exit) {
    Terminate(attempt_graceful_exit);
    return Init();
  }

  virtual RPCChannel* rpc_channel() const = 0;

  // Synchronizes the underlying memory for the pointer before the call.
  absl::Status SynchronizePtrBefore(v::Ptr* ptr);

  // Synchronizes the underlying memory for pointer after the call.
  absl::Status SynchronizePtrAfter(v::Ptr* ptr) const;

  // Makes a call to the sandboxee.
  template <typename... Args>
  absl::Status Call(const std::string& func, v::Callable* ret, Args&&... args) {
    static_assert(sizeof...(Args) <= FuncCall::kArgsMax,
                  "Too many arguments to sapi::Sandbox::Call()");
    return WrapCallStatus(Call(
        func, ret, {internal::PtrOrCallable(std::forward<Args>(args))...}));
  }

  // Allocates memory in the sandboxee, automatic_free indicates whether the
  // memory should be freed on the remote side when the 'var' goes out of scope.
  absl::Status Allocate(v::Var* var, bool automatic_free = false);

  // Frees memory in the sandboxee.
  absl::Status Free(v::Var* var);

  // Finds the address of a symbol in the sandboxee.
  absl::Status Symbol(const char* symname, void** addr);

  // Transfers memory to the sandboxee's address space from the hostcode.
  // Returns the status of the operation. Requires a v::Var object to be set up
  // with a suitable memory buffer allocated in the hostcode.
  //
  // Example Usage:
  //    std::string buffer(size_of_memory_in_sandboxee, ' ');
  //    sapi::v::Array<uint8_t> sapi_buffer(
  //       reinterpret_cast<uint8_t*>(buffer.data()), buffer.size());
  //    SAPI_RETURN_IF_ERROR(sandbox.Allocate(&sapi_buffer));
  //    SAPI_RETURN_IF_ERROR(sandbox.TransferFromSandboxee(&sapi_buffer));
  absl::Status TransferToSandboxee(v::Var* var);

  // Transfers memory from the sandboxee's address space to the hostcode.
  // Returns the status of the operation. Requires a v::Var object to be set up
  // suitable memory buffer allocated in the hostcode. This call
  // does not alter the memory in the sandboxee. It is therefore safe to
  // const_cast `addr_of_memory_in_sandboxee` if necessary.
  //
  // Example Usage:
  //    std::string buffer(size_of_memory_in_sandboxee, ' ');
  //    sapi::v::Array<uint8_t> sapi_buffer(
  //       reinterpret_cast<uint8_t*>(buffer.data()), buffer.size());
  //    sapi_buffer.SetRemote(addr_of_memory_in_sandboxee);
  //    SAPI_RETURN_IF_ERROR(sandbox.TransferFromSandboxee(&sapi_buffer));
  absl::Status TransferFromSandboxee(v::Var* var);

  // Allocates and transfers a buffer to the sandboxee's address space from the
  // hostcode. Returns a status on failure, or a unique_ptr to
  // sapi::v::Array<const uint8_t> on success.
  absl::StatusOr<std::unique_ptr<sapi::v::Array<const uint8_t>>>
  AllocateAndTransferToSandboxee(absl::Span<const uint8_t> buffer);

  absl::StatusOr<std::string> GetCString(const v::RemotePtr& str,
                                         size_t max_length = 10ULL
                                                             << 20 /* 10 MiB*/
  );

  // Waits until the sandbox terminated and returns the result.
  virtual const sandbox2::Result& AwaitResult() = 0;

  virtual absl::Status SetWallTimeLimit(absl::Duration limit) const = 0;

  // Returns the pid of the sandboxee if available. Can return an error if the
  // sandboxee is not running or we're using an in-process sandbox.
  virtual absl::StatusOr<int> GetPid() const = 0;

 protected:
  // WrapCallStatus is called with the status returned by a Call. The default
  // implementation simply returns the status as is.
  // This can be used to convert certain errors to a different form, for example
  // to convert sandbox channel errors to a specific status.
  virtual absl::Status WrapCallStatus(absl::Status status) { return status; }

  absl::Status Call(const std::string& func, v::Callable* ret,
                    std::initializer_list<internal::PtrOrCallable> args);

 private:
  // TODO(sroettger): Remove this function after migrating all users of
  // CreateNotifier() to Sandbox2Backend.
  friend class Sandbox2Backend;

  ABSL_DEPRECATED("Override CreateNotifier() in Sandbox2Backend instead")
  virtual std::unique_ptr<sandbox2::Notify> CreateNotifier() { return nullptr; }
};

// The Sandbox class represents the sandboxed library. It provides users with
// means to communicate with it (make function calls, transfer memory).
template <typename Backend>
class SandboxImpl : public SandboxBase {
 public:
  explicit SandboxImpl(SandboxConfig config)
      : SandboxBase(), backend_(this, std::move(config)) {}

  SandboxImpl(const SandboxImpl&) = delete;
  SandboxImpl& operator=(const SandboxImpl&) = delete;

  virtual ~SandboxImpl() = default;

  // Initializes a new sandboxing session.
  absl::Status Init() override { return backend().Init(); }

  // Returns whether the current sandboxing session is active.
  bool is_active() const override { return backend().is_active(); }

  // Terminates the current sandboxing session (if it exists).
  void Terminate(bool attempt_graceful_exit = true) override {
    backend().Terminate(attempt_graceful_exit);
  }

  ABSL_DEPRECATE_AND_INLINE()
  sandbox2::Comms* comms() const { return backend().comms(); }

  ABSL_DEPRECATE_AND_INLINE()
  int pid() const { return backend().pid(); }

  absl::StatusOr<int> GetPid() const override { return backend().GetPid(); }

  absl::Status SetWallTimeLimit(absl::Duration limit) const override {
    return backend().SetWallTimeLimit(limit);
  }

  // TODO(sroettger): migrate all callers that need the sandbox2::Result to
  // backend().AwaitResult() instead and afterwards make this function return
  // absl::Status.
  const sandbox2::Result& AwaitResult() override {
    return backend().AwaitResult();
  }
  const sandbox2::Result& result() const { return backend().result(); }

  RPCChannel* rpc_channel() const override { return backend().rpc_channel(); }

  Backend& backend() { return backend_; }
  const Backend& backend() const { return backend_; }

 private:
  Backend backend_;
};

using Sandbox = SandboxImpl<Sandbox2Backend>;

}  // namespace sapi

#endif  // SANDBOXED_API_SANDBOX_H_
