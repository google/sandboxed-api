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
#include <ctime>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "sandboxed_api/file_toc.h"
#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/globals.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/vars.h"

namespace sapi {

// Context holding, potentially shared, fork client.
class ForkClientContext {
 public:
  explicit ForkClientContext(const FileToc* embed_lib_toc)
      : embed_lib_toc_(embed_lib_toc) {}

 private:
  friend class Sandbox;
  const FileToc* embed_lib_toc_;
  absl::Mutex mu_;
  std::unique_ptr<sandbox2::ForkClient> client_ ABSL_GUARDED_BY(mu_);
  std::unique_ptr<sandbox2::Executor> executor_ ABSL_GUARDED_BY(mu_);
};

// The Sandbox class represents the sandboxed library. It provides users with
// means to communicate with it (make function calls, transfer memory).
class Sandbox {
 public:
  explicit Sandbox(
      ForkClientContext* fork_client_context ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : fork_client_context_(fork_client_context) {}

  explicit Sandbox(const FileToc* embed_lib_toc ABSL_ATTRIBUTE_LIFETIME_BOUND);

  explicit Sandbox(std::nullptr_t);

  Sandbox(const Sandbox&) = delete;
  Sandbox& operator=(const Sandbox&) = delete;

  virtual ~Sandbox();

  void SetForkClientContext(ForkClientContext* fork_client_context);

  // Initializes a new sandboxing session.
  absl::Status Init(bool use_unotify_monitor = false);

  // Returns whether the current sandboxing session is active.
  bool is_active() const;

  // Terminates the current sandboxing session (if it exists).
  void Terminate(bool attempt_graceful_exit = true);

  // Restarts the sandbox.
  absl::Status Restart(bool attempt_graceful_exit) {
    Terminate(attempt_graceful_exit);
    return Init();
  }

  sandbox2::Comms* comms() const { return comms_; }

  RPCChannel* rpc_channel() const { return rpc_channel_.get(); }

  int pid() const { return pid_; }

  // Synchronizes the underlying memory for the pointer before the call.
  absl::Status SynchronizePtrBefore(v::Callable* ptr);

  // Synchronizes the underlying memory for pointer after the call.
  absl::Status SynchronizePtrAfter(v::Callable* ptr) const;

  // Makes a call to the sandboxee.
  template <typename... Args>
  absl::Status Call(const std::string& func, v::Callable* ret, Args&&... args) {
    static_assert(sizeof...(Args) <= FuncCall::kArgsMax,
                  "Too many arguments to sapi::Sandbox::Call()");
    return Call(func, ret, {std::forward<Args>(args)...});
  }
  virtual absl::Status Call(const std::string& func, v::Callable* ret,
                            std::initializer_list<v::Callable*> args);

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
  const sandbox2::Result& AwaitResult();
  const sandbox2::Result& result() const { return result_; }

  absl::Status SetWallTimeLimit(absl::Duration limit) const;

 protected:

  // Gets extra arguments to be passed to the sandboxee.
  virtual void GetArgs(std::vector<std::string>* args) const {
    args->push_back(absl::StrCat("--stderrthreshold=",
                                 static_cast<int>(absl::StderrThreshold())));
  }

  // Gets the environment variables passed to the sandboxee.
  virtual void GetEnvs(std::vector<std::string>* envs) const {
    // Do nothing by default.
  }

 private:
  // Returns the sandbox policy. Subclasses can modify the default policy
  // builder, or return a completely new policy.
  virtual std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder* builder);

  // Path of the sandboxee:
  //  - relative to runfiles directory: ::sapi::GetDataDependencyFilePath()
  //    will be applied to it,
  //  - absolute: will be used as is.
  virtual std::string GetLibPath() const { return ""; }

  // Modifies the Executor object if needed.
  virtual void ModifyExecutor(sandbox2::Executor* executor) {
    // Do nothing by default.
  }

  // Provides a custom notifier for sandboxee events. May return nullptr.
  virtual std::unique_ptr<sandbox2::Notify> CreateNotifier() { return nullptr; }

  // Exits the sandboxee.
  void Exit() const;

  // The main sandbox2::Sandbox2 object.
  std::unique_ptr<sandbox2::Sandbox2> s2_;
  // Marks whether Sandbox2 result was already fetched.
  // We cannot just delete s2_ as Terminate might be called from another thread
  // and comms object can be still in use then.
  bool s2_awaited_ = false;

  // Result of the most recent sandbox execution
  sandbox2::Result result_;

  // Comms with the sandboxee.
  sandbox2::Comms* comms_ = nullptr;
  // RPCChannel object.
  std::unique_ptr<RPCChannel> rpc_channel_;
  // The main pid of the sandboxee.
  pid_t pid_ = 0;

  // FileTOC with the embedded library, takes precedence over GetLibPath if
  // present (not nullptr).
  const FileToc* embed_lib_toc_;

  ForkClientContext* fork_client_context_;
  // Set if the object owns the client context instance.
  std::unique_ptr<ForkClientContext> owned_fork_client_context_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_SANDBOX_H_
