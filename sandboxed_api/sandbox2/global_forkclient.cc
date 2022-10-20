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

// Implementation of the sandbox2::ForkServer class.

#include "sandboxed_api/sandbox2/global_forkclient.h"

#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/embed_file.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/fork_client.h"
#include "sandboxed_api/sandbox2/forkserver_bin_embed.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/raw_logging.h"

namespace sandbox2 {

namespace file_util = ::sapi::file_util;

namespace {

std::string ToString(GlobalForkserverStartMode mode) {
  switch (mode) {
    case GlobalForkserverStartMode::kOnDemand:
      return "ondemand";
    default:
      return "unknown";
  }
}

}  // namespace

bool AbslParseFlag(absl::string_view text, GlobalForkserverStartModeSet* out,
                   std::string* error) {
  *out = {};
  if (text == "never") {
    return true;
  }
  for (absl::string_view mode : absl::StrSplit(text, ',')) {
    mode = absl::StripAsciiWhitespace(mode);
    if (mode == "ondemand") {
      *out |= GlobalForkserverStartMode::kOnDemand;
    } else {
      *error = absl::StrCat("Invalid forkserver start mode: ", mode);
      return false;
    }
  }
  return true;
}

std::string AbslUnparseFlag(GlobalForkserverStartModeSet in) {
  std::vector<std::string> str_modes;
  for (size_t i = 0; i < GlobalForkserverStartModeSet::kSize; ++i) {
    auto mode = static_cast<GlobalForkserverStartMode>(i);
    if (in.contains(mode)) {
      str_modes.push_back(ToString(mode));
    }
  }
  if (str_modes.empty()) {
    return "never";
  }
  return absl::StrJoin(str_modes, ",");
}

}  // namespace sandbox2

ABSL_FLAG(std::string, sandbox2_forkserver_binary_path, "",
          "Path to forkserver_bin binary");
ABSL_FLAG(sandbox2::GlobalForkserverStartModeSet,
          sandbox2_forkserver_start_mode,
          sandbox2::GlobalForkserverStartModeSet(
              sandbox2::GlobalForkserverStartMode::kOnDemand)
          ,
          "When Sandbox2 Forkserver process should be started");

namespace sandbox2 {

namespace {

GlobalForkserverStartModeSet GetForkserverStartMode() {
  return absl::GetFlag(FLAGS_sandbox2_forkserver_start_mode);
}

absl::StatusOr<std::unique_ptr<GlobalForkClient>> StartGlobalForkServer() {
  SAPI_RAW_LOG(INFO, "Starting global forkserver");

  // Allow passing of a spearate forkserver_bin via flag
  int exec_fd = -1;
  if (!absl::GetFlag(FLAGS_sandbox2_forkserver_binary_path).empty()) {
    exec_fd = open(absl::GetFlag(FLAGS_sandbox2_forkserver_binary_path).c_str(),
                   O_RDONLY);
  }
  if (exec_fd < 0) {
    // For Android we expect the forkserver_bin in the flag
    if constexpr (sapi::host_os::IsAndroid()) {
      return absl::ErrnoToStatus(
          errno,
          "Open init binary passed via --sandbox2_forkserver_binary_path");
    }
    // Extract the fd when it's owned by EmbedFile
    exec_fd = sapi::EmbedFile::instance()->GetDupFdForFileToc(
        forkserver_bin_embed_create());
  }
  if (exec_fd < 0) {
    return absl::InternalError("Getting FD for init binary failed");
  }
  file_util::fileops::FDCloser exec_fd_closer(exec_fd);

  std::string proc_name = "S2-FORK-SERV";

  int sv[2];
  if (socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) == -1) {
    return absl::ErrnoToStatus(errno, "Creating socket pair failed");
  }

  // Fork the fork-server, and clean-up the resources (close remote sockets).
  pid_t pid = util::ForkWithFlags(SIGCHLD);
  if (pid == -1) {
    return absl::ErrnoToStatus(errno, "Forking forkserver process failed");
  }

  // Child.
  if (pid == 0) {
    // Move the comms FD to the proper, expected FD number.
    // The new FD will not be CLOEXEC, which is what we want.
    // If exec_fd == Comms::kSandbox2ClientCommsFD then it would be replaced by
    // the comms fd and result in EACCESS at execveat.
    // So first move exec_fd to another fd number.
    if (exec_fd == Comms::kSandbox2ClientCommsFD) {
      exec_fd = dup(exec_fd);
      SAPI_RAW_PCHECK(exec_fd != -1, "duping exec fd failed");
      fcntl(exec_fd, F_SETFD, FD_CLOEXEC);
    }
    SAPI_RAW_PCHECK(dup2(sv[0], Comms::kSandbox2ClientCommsFD) != -1,
                    "duping comms fd failed");

    char* const args[] = {proc_name.data(), nullptr};
    char* const envp[] = {nullptr};
    syscall(__NR_execveat, exec_fd, "", args, envp, AT_EMPTY_PATH);
    SAPI_RAW_PLOG(FATAL, "Could not launch forkserver binary");
    abort();
  }

  close(sv[0]);
  return std::make_unique<GlobalForkClient>(sv[1], pid);
}

void WaitForForkserver(pid_t pid) {
  int status;
  pid_t wpid = TEMP_FAILURE_RETRY(waitpid(pid, &status, 0));
  if (wpid != pid) {
    SAPI_RAW_PLOG(ERROR, "Waiting for %d failed", pid);
  }
  if (WIFEXITED(status)) {
    int exit_code = WEXITSTATUS(status);
    if (exit_code == 0) {
      SAPI_RAW_LOG(INFO, "forkserver (pid=%d) terminated normally", pid);
    } else {
      SAPI_RAW_LOG(WARNING, "forkserver (pid=%d) terminated with exit code %d",
                   pid, exit_code);
    }
  } else if (WIFSIGNALED(status)) {
    SAPI_RAW_LOG(WARNING, "forkserver (pid=%d) terminated by signal %d", pid,
                 WTERMSIG(status));
  }
}

}  // namespace

absl::Mutex GlobalForkClient::instance_mutex_(absl::kConstInit);
GlobalForkClient* GlobalForkClient::instance_ = nullptr;

void GlobalForkClient::EnsureStarted(GlobalForkserverStartMode mode) {
  absl::MutexLock lock(&instance_mutex_);
  EnsureStartedLocked(mode);
}

void GlobalForkClient::EnsureStartedLocked(GlobalForkserverStartMode mode) {
  if (instance_) {
    return;
  }
  if (getenv(kForkServerDisableEnv)) {
    SAPI_RAW_LOG(ERROR,
                 "Start of the Global Fork-Server prevented by the %s "
                 "environment variable present",
                 kForkServerDisableEnv);
    return;
  }
  if (!GetForkserverStartMode().contains(mode)) {
    SAPI_RAW_LOG(
        ERROR, "Start of the Global Fork-Server prevented by commandline flag");
    return;
  }
  absl::StatusOr<std::unique_ptr<GlobalForkClient>> forkserver =
      StartGlobalForkServer();
  if (!forkserver.ok()) {
    SAPI_RAW_LOG(ERROR, "Starting forkserver failed: %s",
                 forkserver.status().message().data());
  }
  instance_ = forkserver->release();
}

void GlobalForkClient::ForceStart() {
  absl::MutexLock lock(&GlobalForkClient::instance_mutex_);
  SAPI_RAW_CHECK(instance_ == nullptr,
                 "A force start requested when the Global Fork-Server was "
                 "already running");
  absl::StatusOr<std::unique_ptr<GlobalForkClient>> forkserver =
      StartGlobalForkServer();
  SAPI_RAW_CHECK(forkserver.ok(), forkserver.status().message().data());
  instance_ = forkserver->release();
}

void GlobalForkClient::Shutdown() {
  pid_t pid = -1;
  {
    absl::MutexLock lock(&GlobalForkClient::instance_mutex_);
    if (instance_) {
      pid = instance_->fork_client_.pid();
    }
    delete instance_;
    instance_ = nullptr;
  }
  if (pid != -1) {
    WaitForForkserver(pid);
  }
}

pid_t GlobalForkClient::SendRequest(const ForkRequest& request, int exec_fd,
                                    int comms_fd, int user_ns_fd,
                                    pid_t* init_pid) {
  absl::ReleasableMutexLock lock(&GlobalForkClient::instance_mutex_);
  EnsureStartedLocked(GlobalForkserverStartMode::kOnDemand);
  if (!instance_) {
    return -1;
  }
  pid_t pid = instance_->fork_client_.SendRequest(request, exec_fd, comms_fd,
                                                  user_ns_fd, init_pid);
  if (instance_->comms_.IsTerminated()) {
    LOG(ERROR) << "Global forkserver connection terminated";
    pid_t server_pid = instance_->fork_client_.pid();
    delete instance_;
    instance_ = nullptr;
    // Don't wait for process exit while still holding the lock and potentially
    // blocking other threads.
    lock.Release();
    WaitForForkserver(server_pid);
  }
  return pid;
}

pid_t GlobalForkClient::GetPid() {
  absl::MutexLock lock(&instance_mutex_);
  EnsureStartedLocked(GlobalForkserverStartMode::kOnDemand);
  if (!instance_) {
    return -1;
  }
  return instance_->fork_client_.pid();
}

bool GlobalForkClient::IsStarted() {
  absl::ReaderMutexLock lock(&instance_mutex_);
  return instance_ != nullptr;
}
}  // namespace sandbox2
