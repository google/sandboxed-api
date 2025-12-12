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
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "sandboxed_api/file_toc.h"
#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/globals.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/fork_client.h"
#include "sandboxed_api/sandbox2/limits.h"
#include "sandboxed_api/sandbox2/notify.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2_rpcchannel.h"
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

// Context holding, potentially shared, fork client.
class ForkClientContext {
 public:
  explicit ForkClientContext(const FileToc* embed_lib_toc)
      : sandboxee_source_(embed_lib_toc) {}
  // Path of the sandboxee:
  //  - relative to runfiles directory: ::sapi::GetDataDependencyFilePath()
  //    will be applied to it,
  //  - absolute: will be used as is.
  explicit ForkClientContext(std::string lib_path)
      : sandboxee_source_(std::move(lib_path)) {}

 private:
  friend class Sandbox;

  // TODO(sroettger): this is optional until we migrated users of GetLibPath().
  std::optional<std::variant<const FileToc*, std::string>> sandboxee_source_;
  struct SharedState {
    absl::Mutex mu_;
    std::shared_ptr<sandbox2::ForkClient> client_ ABSL_GUARDED_BY(mu_);
    std::shared_ptr<sandbox2::Executor> executor_ ABSL_GUARDED_BY(mu_);
  };
  std::shared_ptr<SharedState> shared_ = std::make_shared<SharedState>();
};

struct Sandbox2Config {
  // Optional. If not set, the default policy will be used.
  // See DefaultPolicyBuilder().
  // Can be overridden by Sandbox::ModifyPolicy().
  // TODO(sroettger): remove ModifyPolicy() once all users are migrated.
  std::unique_ptr<sandbox2::Policy> policy;

  // Includes the path to the sandboxee. Optional only if the generated embedded
  // sandboxee class is used.
  std::optional<ForkClientContext> fork_client_context;

  bool use_unotify_monitor = false;
  bool enable_log_server = false;
  std::optional<std::string> cwd;
  std::optional<sandbox2::Limits> limits;

  // A generic policy which should work with majority of typical libraries,
  // which are single-threaded and require ~30 basic syscalls.
  static sandbox2::PolicyBuilder DefaultPolicyBuilder();
};

struct SandboxConfig {
  std::optional<std::vector<std::string>> environment_variables;
  Sandbox2Config sandbox2;

  static std::vector<std::string> DefaultEnvironmentVariables() {
    return {
    };
  }
};

// The Sandbox class represents the sandboxed library. It provides users with
// means to communicate with it (make function calls, transfer memory).
class Sandbox {
 public:
  explicit Sandbox(SandboxConfig config);

  // TODO(sroettger): Remove all constructors below once all callers have been
  // migrated to the new constructor.
  ABSL_DEPRECATED("Use Sandbox(SandboxConfig) instead")
  Sandbox(SandboxConfig config,
          const FileToc* embed_lib_toc ABSL_ATTRIBUTE_LIFETIME_BOUND);
  ABSL_DEPRECATED("Use Sandbox(SandboxConfig) instead")
  explicit Sandbox(const FileToc* embed_lib_toc ABSL_ATTRIBUTE_LIFETIME_BOUND);
  ABSL_DEPRECATED("Use Sandbox(SandboxConfig) instead")
  explicit Sandbox(std::nullptr_t);
  ABSL_DEPRECATED("Use Sandbox(SandboxConfig) instead")
  Sandbox(SandboxConfig config, std::nullptr_t);
  ABSL_DEPRECATED("Use Sandbox(SandboxConfig) instead")
  Sandbox(ForkClientContext* fork_client_context);

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
  const sandbox2::Result& AwaitResult();
  const sandbox2::Result& result() const { return result_; }

  absl::Status SetWallTimeLimit(absl::Duration limit) const;

 protected:

  // Gets extra arguments to be passed to the sandboxee.
  virtual void GetArgs(std::vector<std::string>* args) const {
    args->push_back(absl::StrCat("--stderrthreshold=",
                                 static_cast<int>(absl::StderrThreshold())));
  }

  // TODO(sroettger): Remove this method once all callers have been migrated to
  // the new constructor.
  virtual void GetEnvs(std::vector<std::string>* envs) const {
    *envs = SandboxConfig::DefaultEnvironmentVariables();
  }

  // WrapCallStatus is called with the status returned by a Call. The default
  // implementation simply returns the status as is.
  // This can be used to convert certain errors to a different form, for example
  // to convert sandbox channel errors to a specific status.
  virtual absl::Status WrapCallStatus(absl::Status status) { return status; }

 private:
  absl::Status Call(const std::string& func, v::Callable* ret,
                    std::initializer_list<internal::PtrOrCallable> args);
  // Returns the sandbox policy. Subclasses can modify the default policy
  // builder, or return a completely new policy.
  virtual std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder* builder);

  virtual std::string GetLibPath() const;

  // Modifies the Executor object if needed.
  virtual void ModifyExecutor(sandbox2::Executor* executor) {
    // Do nothing by default.
  }

  void ApplySandbox2Config(sandbox2::Executor* executor) const;

  // Provides a custom notifier for sandboxee events. May return nullptr.
  virtual std::unique_ptr<sandbox2::Notify> CreateNotifier() { return nullptr; }

  std::vector<std::string> EnvironmentVariables() const {
    if (config_.environment_variables.has_value()) {
      return *config_.environment_variables;
    }
    std::vector<std::string> envs;
    // TODO(sroettger): Remove the GetEnvs call once all callers have been
    // migrated to the new constructor.
    GetEnvs(&envs);
    return envs;
  }

  const ForkClientContext& fork_client_context() const {
    return config_.sandbox2.fork_client_context.value();
  }
  ForkClientContext::SharedState& fork_client_shared() const {
    return *fork_client_context().shared_;
  }

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

  SandboxConfig config_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_SANDBOX_H_
