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

#ifndef SANDBOXED_API_SANDBOX2_EXECUTOR_H_
#define SANDBOXED_API_SANDBOX2_EXECUTOR_H_

#include <stdlib.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "sandboxed_api/sandbox2/fork_client.h"
#include "sandboxed_api/sandbox2/ipc.h"
#include "sandboxed_api/sandbox2/limits.h"
#include "sandboxed_api/sandbox2/namespace.h"
#include "sandboxed_api/util/fileops.h"

namespace sandbox2 {

// The sandbox2::Executor class is responsible for both creating and executing
// new processes which will be sandboxed.
class Executor final {
 public:
  struct Process {
    pid_t init_pid = -1;
    pid_t main_pid = -1;
  };

  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;

  // Initialized with a path to the process that the Executor class will
  // execute
  Executor(
      absl::string_view path, absl::Span<const std::string> argv,
      absl::Span<const std::string> envp = absl::MakeConstSpan(CopyEnviron()))
      : path_(std::string(path)),
        argv_(argv.begin(), argv.end()),
        envp_(envp.begin(), envp.end()) {
    CHECK(!path_.empty());
    SetUpServerSideCommsFd();
  }

  // Executor will own this file-descriptor, so if intend to use it, pass here
  // dup(fd) instead
  Executor(int exec_fd, absl::Span<const std::string> argv,
           absl::Span<const std::string> envp)
      : exec_fd_(exec_fd),
        argv_(argv.begin(), argv.end()),
        envp_(envp.begin(), envp.end()) {
    CHECK_GE(exec_fd, 0);
    SetUpServerSideCommsFd();
  }

  // Uses a custom ForkServer (which the supplied ForkClient can communicate
  // with), which knows how to fork (or even execute) new sandboxed processes
  // (hence, no need to supply path/argv/envp here)
  explicit Executor(ForkClient* fork_client)
      : enable_sandboxing_pre_execve_(false), fork_client_(fork_client) {
    CHECK(fork_client != nullptr);
    SetUpServerSideCommsFd();
  }

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
      : libunwind_sbox_for_pid_(libunwind_sbox_for_pid),
        enable_sandboxing_pre_execve_(false) {
    CHECK_GE(libunwind_sbox_for_pid_, 0);
    SetUpServerSideCommsFd();
  }

  // Creates a copy of the environment
  static std::vector<std::string> CopyEnviron();

  // Creates a server-side Comms end-point using a pre-connected file
  // descriptor.
  void SetUpServerSideCommsFd();

  // Starts a new process which is connected with this Executor instance via a
  // Comms channel.
  // For clone_flags refer to Linux' 'man 2 clone'.
  //
  // caps is a vector of capabilities that are kept in the permitted set after
  // the clone, use with caution.
  absl::StatusOr<Process> StartSubProcess(int clone_flags,
                                          const Namespace* ns = nullptr,
                                          const std::vector<int>& caps = {});

  // Whether the Executor has been started yet
  bool started_ = false;

  // If this executor is running the libunwind sandbox for a process,
  // this variable will hold the PID of the process. Otherwise it is zero.
  pid_t libunwind_sbox_for_pid_ = 0;

  // Should the sandboxing be enabled before execve() occurs, or the binary will
  // do it by itself, using the Client object's methods
  bool enable_sandboxing_pre_execve_ = true;

  // Alternate (path/fd)/argv/envp to be used the in the __NR_execve call.
  sapi::file_util::fileops::FDCloser exec_fd_;
  std::string path_;
  std::vector<std::string> argv_;
  std::vector<std::string> envp_;

  // chdir to cwd_, if set. Defaults to current working directory.
  std::string cwd_ = []() {
    std::string cwd = sapi::file_util::fileops::GetCWD();
    if (cwd.empty()) {
      PLOG(WARNING) << "Getting current working directory";
    }
    return cwd;
  }();

  // Client (sandboxee) end-point of a socket-pair used to create Comms channel
  sapi::file_util::fileops::FDCloser client_comms_fd_;

  // ForkClient connecting to the ForkServer - not owned by the object
  ForkClient* fork_client_ = nullptr;

  IPC ipc_;        // Used for communication with the sandboxee
  Limits limits_;  // Defines server- and client-side limits
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_EXECUTOR_H_
