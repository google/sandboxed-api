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

// Implementation of the sandbox2::Executor class

#include "sandboxed_api/sandbox2/executor.h"

#include <fcntl.h>
#include <libgen.h>
#include <sys/socket.h>
#include <unistd.h>

#include <climits>
#include <cstddef>
#include <memory>
#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/fork_client.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/sandbox2/global_forkclient.h"
#include "sandboxed_api/sandbox2/ipc.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/raw_logging.h"

namespace sandbox2 {

namespace file_util = ::sapi::file_util;

namespace {
void DisableCompressStackDepot(ForkRequest& request) {
  auto disable_compress_stack_depot = [&request](absl::string_view sanitizer) {
    auto prefix = absl::StrCat(sanitizer, "_OPTIONS=");
    auto it = std::find_if(request.mutable_envs()->begin(),
                           request.mutable_envs()->end(),
                           [&prefix](const std::string& env) {
                             return absl::StartsWith(env, prefix);
                           });
    constexpr absl::string_view option = "compress_stack_depot=0";
    if (it != request.mutable_envs()->end()) {
      // If it's already there, the last value will be used.
      absl::StrAppend(&*it, ":", option);
      return;
    }
    request.add_envs(absl::StrCat(prefix, option));
  };
  if constexpr (sapi::sanitizers::IsASan()) {
    disable_compress_stack_depot("ASAN");
  }
  if constexpr (sapi::sanitizers::IsMSan()) {
    disable_compress_stack_depot("MSAN");
  }
  if constexpr (sapi::sanitizers::IsLSan()) {
    disable_compress_stack_depot("LSAN");
  }
  if constexpr (sapi::sanitizers::IsHwASan()) {
    disable_compress_stack_depot("HWSAN");
  }
  if constexpr (sapi::sanitizers::IsTSan()) {
    disable_compress_stack_depot("TSAN");
  }
}
}  // namespace

std::vector<std::string> Executor::CopyEnviron() {
  return util::CharPtrArray(environ).ToStringVector();
}

absl::StatusOr<Executor::Process> Executor::StartSubProcess(
    int32_t clone_flags, const Namespace* ns, const std::vector<int>& caps) {
  if (started_) {
    return absl::FailedPreconditionError(
        "This executor has already been started");
  }

  if (!path_.empty()) {
    exec_fd_ = file_util::fileops::FDCloser(open(path_.c_str(), O_PATH));
    if (exec_fd_.get() < 0) {
      if (errno == ENOENT) {
        return absl::ErrnoToStatus(errno, path_);
      }
      return absl::ErrnoToStatus(errno,
                                 absl::StrCat("Could not open file ", path_));
    }
  }

  if (libunwind_sbox_for_pid_ != 0) {
    VLOG(1) << "StartSubProcces, starting libunwind";
  } else if (exec_fd_.get() < 0) {
    VLOG(1) << "StartSubProcess, with [Fork-Server]";
  } else if (!path_.empty()) {
    VLOG(1) << "StartSubProcess, with file " << path_;
  } else {
    VLOG(1) << "StartSubProcess, with fd " << exec_fd_.get();
  }

  ForkRequest request;
  *request.mutable_args() = {argv_.begin(), argv_.end()};
  *request.mutable_envs() = {envp_.begin(), envp_.end()};

  // Add LD_ORIGIN_PATH to envs, as it'll make the amount of syscalls invoked by
  // ld.so smaller.
  if (!path_.empty()) {
    request.add_envs(absl::StrCat("LD_ORIGIN_PATH=",
                                  file_util::fileops::StripBasename(path_)));
  }

  // Disable optimization to avoid related syscalls.
  if constexpr (sapi::sanitizers::IsAny()) {
    DisableCompressStackDepot(request);
  }

  // If neither the path, nor exec_fd is specified, just assume that we need to
  // send a fork request.
  //
  // Otherwise, it's either sandboxing pre- or post-execve with the global
  // Fork-Server.
  if (libunwind_sbox_for_pid_ != 0) {
    request.set_mode(FORKSERVER_FORK_JOIN_SANDBOX_UNWIND);
  } else if (exec_fd_.get() == -1) {
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
    request.set_allow_mount_propagation(ns->allow_mount_propagation());
  }

  request.set_clone_flags(clone_flags);

  for (auto cap : caps) {
    request.add_capabilities(cap);
  }

  file_util::fileops::FDCloser ns_fd;
  if (libunwind_sbox_for_pid_ != 0) {
    const std::string ns_path =
        absl::StrCat("/proc/", libunwind_sbox_for_pid_, "/ns/user");
    ns_fd = file_util::fileops::FDCloser(open(ns_path.c_str(), O_RDONLY));
    if (ns_fd.get() == -1) {
      return absl::ErrnoToStatus(
          errno, absl::StrCat("Could not open user ns fd (", ns_path, ")"));
    }
  }

  Process process;

  if (fork_client_) {
    process.main_pid = fork_client_->SendRequest(
        request, exec_fd_.get(), client_comms_fd_.get(), ns_fd.get(),
        &process.init_pid);
  } else {
    process.main_pid = GlobalForkClient::SendRequest(
        request, exec_fd_.get(), client_comms_fd_.get(), ns_fd.get(),
        &process.init_pid);
  }

  started_ = true;

  client_comms_fd_.Close();
  exec_fd_.Close();

  VLOG(1) << "StartSubProcess returned with: " << process.main_pid;
  return process;
}

std::unique_ptr<ForkClient> Executor::StartForkServer() {
  // This flag is set explicitly to 'true' during object instantiation, and
  // custom fork-servers should never be sandboxed.
  set_enable_sandbox_before_exec(false);
  absl::StatusOr<Process> process = StartSubProcess(0);
  if (!process.ok()) {
    return nullptr;
  }
  return std::make_unique<ForkClient>(process->main_pid, ipc_.comms());
}

void Executor::SetUpServerSideCommsFd() {
  int sv[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
    PLOG(FATAL) << "socketpair(AF_UNIX, SOCK_STREAM) failed";
  }

  client_comms_fd_ = file_util::fileops::FDCloser(sv[0]);
  ipc_.SetUpServerSideComms(sv[1]);
}

}  // namespace sandbox2
