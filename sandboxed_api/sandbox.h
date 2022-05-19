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

#include <memory>
#include <string>
#include <vector>

#include "sandboxed_api/file_toc.h"
#include "absl/base/macros.h"
#include "absl/status/statusor.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/vars.h"

namespace sapi {

// The Sandbox class represents the sandboxed library. It provides users with
// means to communicate with it (make function calls, transfer memory).
class Sandbox {
 public:
  explicit Sandbox(const FileToc* embed_lib_toc)
      : embed_lib_toc_(embed_lib_toc) {}

  Sandbox(const Sandbox&) = delete;
  Sandbox& operator=(const Sandbox&) = delete;

  virtual ~Sandbox();

  // Initializes a new sandboxing session.
  absl::Status Init();

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
  absl::Status Call(const std::string& func, v::Callable* ret,
                    std::initializer_list<v::Callable*> args);

  // Allocates memory in the sandboxee, automatic_free indicates whether the
  // memory should be freed on the remote side when the 'var' goes out of scope.
  absl::Status Allocate(v::Var* var, bool automatic_free = false);

  // Frees memory in the sandboxee.
  absl::Status Free(v::Var* var);

  // Finds the address of a symbol in the sandboxee.
  absl::Status Symbol(const char* symname, void** addr);

  // Transfers memory (both directions). Status is returned (memory transfer
  // succeeded/failed).
  absl::Status TransferToSandboxee(v::Var* var);
  absl::Status TransferFromSandboxee(v::Var* var);

  absl::StatusOr<std::string> GetCString(const v::RemotePtr& str,
                                         size_t max_length = 10ULL
                                                             << 20 /* 10 MiB*/
  );

  // Waits until the sandbox terminated and returns the result.
  const sandbox2::Result& AwaitResult();
  const sandbox2::Result& result() const { return result_; }

  absl::Status SetWallTimeLimit(absl::Duration limit) const;
  ABSL_DEPRECATED(
      "Use sapi::Sandbox::SetWallTimeLimit(absl::Duration) overload instead")
  absl::Status SetWallTimeLimit(time_t limit) const {
    return this->SetWallTimeLimit(absl::Seconds(limit));
  }

 protected:

  // Gets extra arguments to be passed to the sandboxee.
  virtual void GetArgs(std::vector<std::string>* args) const {
    // Do nothing by default.
  }

 private:
  // Gets the environment variables passed to the sandboxee.
  virtual void GetEnvs(std::vector<std::string>* envs) const {
    envs->push_back("GOOGLE_LOGTOSTDERR=1");
  }

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

  // The client to the library forkserver.
  std::unique_ptr<sandbox2::ForkClient> fork_client_;
  std::unique_ptr<sandbox2::Executor> forkserver_executor_;

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
};

}  // namespace sapi

#endif  // SANDBOXED_API_SANDBOX_H_
