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

#ifndef SANDBOXED_API_SANDBOX2_EXECUTOR_H_
#define SANDBOXED_API_SANDBOX2_EXECUTOR_H_

#include <stdlib.h>
#include <sys/capability.h>
#include <unistd.h>
#include <memory>
#include <string>
#include <vector>

#include <glog/logging.h>
#include "absl/base/macros.h"
#include "sandboxed_api/sandbox2/forkserver.h"
#include "sandboxed_api/sandbox2/ipc.h"
#include "sandboxed_api/sandbox2/limits.h"
#include "sandboxed_api/sandbox2/namespace.h"

namespace sandbox2 {

// The sandbox2::Executor class is responsible for both creating and executing
// new processes which will be sandboxed.
class Executor final {
 public:
  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;

  // Initialized with a path to the process that the Executor class will
  // execute
  Executor(const std::string& path, const std::vector<std::string>& argv)
      : Executor(/*exec_fd=*/-1, path, argv, CopyEnviron(),
                 /*enable_sandboxing_pre_execve=*/true,
                 /*libunwind_sbox_for_pid=*/0,
                 /*fork_client=*/nullptr) {}

  // As above, but takes an explicit environment
  Executor(const std::string& path, const std::vector<std::string>& argv,
           const std::vector<std::string>& envp)
      : Executor(/*exec_fd=*/-1, path, argv, envp,
                 /*enable_sandboxing_pre_execve=*/true,
                 /*libunwind_sbox_for_pid=*/0,
                 /*fork_client=*/nullptr) {}

  // As above, but takes a file-descriptor referring to an executable file.
  // Executor will own this file-descriptor, so if intend to use it, pass here
  // dup(fd) instead
  Executor(int exec_fd, const std::vector<std::string>& argv,
           const std::vector<std::string>& envp)
      : Executor(exec_fd, /*path=*/"", argv, envp,
                 /*enable_sandboxing_pre_execve=*/true,
                 /*libunwind_sbox_for_pid=*/0,
                 /*fork_client=*/nullptr) {}

  // Uses a custom ForkServer (which the supplied ForkClient can communicate
  // with), which knows how to fork (or even execute) new sandboxed processes
  // (hence, no need to supply path/argv/envp here)
  explicit Executor(ForkClient* fork_client)
      : Executor(/*exec_fd=*/-1, /*path=*/"", /*argv=*/{}, /*envp=*/{},
                 /*enable_sandboxing_pre_execve=*/false,
                 /*libunwind_sbox_for_pid=*/0, fork_client) {}

  ~Executor();

  // Creates a new process which will act as a custom ForkServer. Should be used
  // with custom fork servers only.
  // This function returns immediately and returns a nullptr on failure.
  std::unique_ptr<ForkClient> StartForkServer();

  // Accessors
  IPC* ipc() { return &ipc_; }

  Limits* limits() { return &limits_; }

  Executor& set_enable_sandbox_before_exec(bool value) {
    enable_sandboxing_pre_execve_ = value;
    return *this;
  }

  Executor& set_cwd(std::string value) {
    cwd_ = std::move(value);
    return *this;
  }

 private:
  friend class Monitor;
  friend class StackTracePeer;

  // Internal constructor for executing libunwind on the given pid
  // enable_sandboxing_pre_execve=false as we are not going to execve.
  explicit Executor(pid_t libunwind_sbox_for_pid)
      : Executor(/*exec_fd=*/-1, /*path=*/"", /*argv=*/{}, /*envp=*/{},
                 /*enable_sandboxing_pre_execve=*/false,
                 /*libunwind_sbox_for_pid=*/libunwind_sbox_for_pid,
                 /*fork_client=*/nullptr) {}

  // Delegate constructor that gets called by the public ones.
  Executor(int exec_fd, const std::string& path,
           const std::vector<std::string>& argv,
           const std::vector<std::string>& envp,
           bool enable_sandboxing_pre_execve, pid_t libunwind_sbox_for_pid,
           ForkClient* fork_client);

  // Creates a copy of the environment
  static std::vector<std::string> CopyEnviron();

  // Creates a server-side Comms end-point using a pre-connected file
  // descriptor.
  void SetUpServerSideCommsFd();

  // Sets the default value for cwd_
  void SetDefaultCwd() {
    char* cwd = get_current_dir_name();
    PCHECK(cwd != nullptr);
    cwd_ = cwd;
    free(cwd);
  }

  // Starts a new process which is connected with this Executor instance via a
  // Comms channel.
  // For clone_flags refer to Linux' 'man 2 clone'.
  //
  // caps is a vector of capabilities that are kept in the permitted set after
  // the clone, use with caution.
  //
  // Returns the same values as fork().
  pid_t StartSubProcess(int clone_flags, const Namespace* ns = nullptr,
                        const std::vector<cap_value_t>* caps = nullptr,
                        pid_t* init_pid_out = nullptr);

  // Whether the Executor has been started yet
  bool started_ = false;

  // If this executor is running the libunwind sandbox for a process,
  // this variable will hold the PID of the process. Otherwise it is zero.
  pid_t libunwind_sbox_for_pid_;

  // Should the sandboxing be enabled before execve() occurs, or the binary will
  // do it by itself, using the Client object's methods
  bool enable_sandboxing_pre_execve_;

  // Alternate (path/fd)/argv/envp to be used the in the __NR_execve call.
  int exec_fd_;
  std::string path_;
  std::vector<std::string> argv_;
  std::vector<std::string> envp_;

  // chdir to cwd_, if set.
  std::string cwd_;

  // Server (sandbox) end-point of a socket-pair used to create Comms channel
  int server_comms_fd_ = -1;
  // Client (sandboxee) end-point of a socket-pair used to create Comms channel
  int client_comms_fd_ = -1;

  // ForkClient connecting to the ForkServer - not owned by the object
  ForkClient* fork_client_;

  IPC ipc_;        // Used for communication with the sandboxee
  Limits limits_;  // Defines server- and client-side limits
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_EXECUTOR_H_
