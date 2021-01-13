// Copyright 2019 Google LLC
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

// Implementation of the sandbox2::Executor class

#include "sandboxed_api/sandbox2/executor.h"

#include <fcntl.h>
#include <libgen.h>
#include <sys/socket.h>
#include <unistd.h>

#include <climits>
#include <cstddef>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/sandbox2/fork_client.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/sandbox2/global_forkclient.h"
#include "sandboxed_api/sandbox2/ipc.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/util/fileops.h"

namespace file_util = ::sapi::file_util;

namespace sandbox2 {

// Delegate constructor that gets called by the public ones.
Executor::Executor(int exec_fd, const std::string& path,
                   const std::vector<std::string>& argv,
                   const std::vector<std::string>& envp,
                   bool enable_sandboxing_pre_execve,
                   pid_t libunwind_sbox_for_pid, ForkClient* fork_client)
    : libunwind_sbox_for_pid_(libunwind_sbox_for_pid),
      enable_sandboxing_pre_execve_(enable_sandboxing_pre_execve),
      exec_fd_(exec_fd),
      path_(path),
      argv_(argv),
      envp_(envp),
      fork_client_(fork_client) {
  if (fork_client_ != nullptr) {
    CHECK(exec_fd == -1 && path.empty());
  } else {
    CHECK((exec_fd == -1 && (!path.empty() || libunwind_sbox_for_pid > 0)) ||
          (exec_fd >= 0 && path.empty()));
  }
  SetUpServerSideCommsFd();
  SetDefaultCwd();
}

Executor::~Executor() {
  if (client_comms_fd_ != -1) {
    close(client_comms_fd_);
  }
}

std::vector<std::string> Executor::CopyEnviron() {
  std::vector<std::string> environ_copy;
  util::CharPtrArrToVecString(environ, &environ_copy);
  return environ_copy;
}

pid_t Executor::StartSubProcess(int32_t clone_flags, const Namespace* ns,
                                const std::vector<int>* caps,
                                pid_t* init_pid_out) {
  if (started_) {
    LOG(ERROR) << "This executor has already been started";
    return -1;
  }

  if (!path_.empty()) {
    exec_fd_ = open(path_.c_str(), O_PATH);
    if (exec_fd_ < 0) {
      PLOG(ERROR) << "Could not open file " << path_;
      return -1;
    }
  }

  if (libunwind_sbox_for_pid_ != 0) {
    VLOG(1) << "StartSubProcces, starting libunwind";
  } else if (exec_fd_ < 0) {
    VLOG(1) << "StartSubProcess, with [Fork-Server]";
  } else if (!path_.empty()) {
    VLOG(1) << "StartSubProcess, with file " << path_;
  } else {
    VLOG(1) << "StartSubProcess, with fd " << exec_fd_;
  }

  ForkRequest request;
  for (size_t i = 0; i < argv_.size(); i++) {
    request.add_args(argv_[i]);
  }
  for (size_t i = 0; i < envp_.size(); i++) {
    request.add_envs(envp_[i]);
  }

  // Add LD_ORIGIN_PATH to envs, as it'll make the amount of syscalls invoked by
  // ld.so smaller. See http://b/7626303 for more details on this behavior.
  if (!path_.empty()) {
    request.add_envs(absl::StrCat("LD_ORIGIN_PATH=",
                                  file_util::fileops::StripBasename(path_)));
  }

  // If neither the path, nor exec_fd is specified, just assume that we need to
  // send a fork request.
  //
  // Otherwise, it's either sandboxing pre- or post-execve with the global
  // Fork-Server.
  if (libunwind_sbox_for_pid_ != 0) {
    request.set_mode(FORKSERVER_FORK_JOIN_SANDBOX_UNWIND);
  } else if (exec_fd_ == -1) {
    request.set_mode(FORKSERVER_FORK);
  } else if (enable_sandboxing_pre_execve_) {
    request.set_mode(FORKSERVER_FORK_EXECVE_SANDBOX);
  } else {
    request.set_mode(FORKSERVER_FORK_EXECVE);
  }

  if (ns) {
    clone_flags |= ns->GetCloneFlags();
    *request.mutable_mount_tree() = ns->mounts().GetMountTree();
    request.set_hostname(ns->hostname());
  }

  request.set_clone_flags(clone_flags);

  if (caps) {
    for (auto cap : *caps) {
      request.add_capabilities(cap);
    }
  }

  int ns_fd = -1;
  if (libunwind_sbox_for_pid_ != 0) {
    std::string ns_path =
        absl::StrCat("/proc/", libunwind_sbox_for_pid_, "/ns/user");
    PCHECK((ns_fd = open(ns_path.c_str(), O_RDONLY)) != -1)
        << "Could not open user ns fd (" << ns_path << ")";
  }

  pid_t init_pid = -1;

  pid_t sandboxee_pid;
  if (fork_client_) {
    sandboxee_pid = fork_client_->SendRequest(
        request, exec_fd_, client_comms_fd_, ns_fd, &init_pid);
  } else {
    sandboxee_pid = GlobalForkClient::SendRequest(
        request, exec_fd_, client_comms_fd_, ns_fd, &init_pid);
  }

  if (init_pid < 0) {
    LOG(ERROR) << "Could not obtain init PID";
  } else if (init_pid == 0 && request.clone_flags() & CLONE_NEWPID) {
    LOG(FATAL)
        << "No init process was spawned even though a PID NS was created, "
        << "potential logic bug";
  }

  if (init_pid_out) {
    *init_pid_out = init_pid;
  }

  started_ = true;

  close(client_comms_fd_);
  client_comms_fd_ = -1;
  if (exec_fd_ >= 0) {
    close(exec_fd_);
    exec_fd_ = -1;
  }

  if (ns_fd >= 0) {
    close(ns_fd);
  }

  VLOG(1) << "StartSubProcess returned with: " << sandboxee_pid;
  return sandboxee_pid;
}

std::unique_ptr<ForkClient> Executor::StartForkServer() {
  // This flag is set explicitly to 'true' during object instantiation, and
  // custom fork-servers should never be sandboxed.
  set_enable_sandbox_before_exec(false);
  pid_t pid = StartSubProcess(0);
  if (pid == -1) {
    return nullptr;
  }
  return absl::make_unique<ForkClient>(pid, ipc_.comms());
}

void Executor::SetUpServerSideCommsFd() {
  int sv[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
    PLOG(FATAL) << "socketpair(AF_UNIX, SOCK_STREAM) failed";
  }

  client_comms_fd_ = sv[0];
  server_comms_fd_ = sv[1];

  ipc_.SetUpServerSideComms(server_comms_fd_);
}

}  // namespace sandbox2
