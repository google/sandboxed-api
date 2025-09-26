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
#include <sched.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <memory>
#include <string>

#include "absl/base/const_init.h"
#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
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

GlobalForkserverStartModeSet GetForkserverStartMode() {
  return absl::GetFlag(FLAGS_sandbox2_forkserver_start_mode);
}

struct ForkserverArgs {
  int exec_fd;
  int comms_fd;
};

int LaunchForkserver(void* vargs) {
  auto* args = static_cast<ForkserverArgs*>(vargs);
  // Move the comms FD to the proper, expected FD number.
  // The new FD will not be CLOEXEC, which is what we want.
  // If exec_fd == Comms::kSandbox2ClientCommsFD then it would be replaced by
  // the comms fd and result in EACCESS at execveat.
  // So first move exec_fd to another fd number.
  if (args->exec_fd == Comms::kSandbox2ClientCommsFD) {
    args->exec_fd = dup(args->exec_fd);
    SAPI_RAW_PCHECK(args->exec_fd != -1, "duping exec fd failed");
    fcntl(args->exec_fd, F_SETFD, FD_CLOEXEC);
  }
  SAPI_RAW_PCHECK(dup2(args->comms_fd, Comms::kSandbox2ClientCommsFD) != -1,
                  "duping comms fd failed");

  char proc_name[] = "S2-FORK-SERV";
  char* const argv[] = {proc_name, nullptr};
  util::Execveat(args->exec_fd, "", argv, environ, AT_EMPTY_PATH);
  SAPI_RAW_PLOG(FATAL, "Could not launch forkserver binary");
}

absl::StatusOr<std::unique_ptr<GlobalForkClient>> StartGlobalForkServer() {
  SAPI_RAW_LOG(INFO, "Starting global forkserver");

  // Allow passing of a separate forkserver_bin via flag
  int exec_fd = -1;
  std::string bin_path = absl::GetFlag(FLAGS_sandbox2_forkserver_binary_path);
  if (!bin_path.empty()) {
    exec_fd = open(bin_path.c_str(), O_RDONLY);
    if (exec_fd < 0) {
      return absl::ErrnoToStatus(
          errno, absl::StrCat("Opening forkserver binary passed via "
                              "--sandbox2_forkserver_binary_path (",
                              bin_path, ")"));
    }
  }
  if (exec_fd < 0) {
    // Extract the fd when it's owned by EmbedFile
    exec_fd = sapi::EmbedFile::instance()->GetDupFdForFileToc(
        forkserver_bin_embed_create());
  }
  if (exec_fd < 0) {
    return absl::InternalError("Getting FD for init binary failed");
  }
  file_util::fileops::FDCloser exec_fd_closer(exec_fd);

  int sv[2];
  if (socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) == -1) {
    return absl::ErrnoToStatus(errno, "Creating socket pair failed");
  }

  // Fork the fork-server, and clean-up the resources (close remote sockets).
  const size_t stack_size = PTHREAD_STACK_MIN;
  int clone_flags = CLONE_VM | CLONE_VFORK | SIGCHLD;
  // CLONE_VM does not play well with TSan.
  if constexpr (sapi::sanitizers::IsTSan()) {
    clone_flags &= ~CLONE_VM & ~CLONE_VFORK;
  }
  char* stack =
      static_cast<char*>(mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0));
  if (stack == MAP_FAILED) {
    return absl::ErrnoToStatus(errno, "Allocating stack failed");
  }
  absl::Cleanup stack_dealloc = [stack, stack_size] {
    munmap(stack, stack_size);
  };
  ForkserverArgs args = {
      .exec_fd = exec_fd,
      .comms_fd = sv[0],
  };
  pid_t pid = clone(LaunchForkserver, &stack[stack_size], clone_flags, &args,
                    nullptr, nullptr, nullptr);
  if (pid == -1) {
    return absl::ErrnoToStatus(errno, "Forking forkserver process failed");
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
    return;
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
  SAPI_RAW_CHECK(forkserver.ok(), forkserver.status().ToString().c_str());
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

SandboxeeProcess GlobalForkClient::SendRequest(const ForkRequest& request,
                                               int exec_fd, int comms_fd) {
  absl::ReleasableMutexLock lock(&GlobalForkClient::instance_mutex_);
  EnsureStartedLocked(GlobalForkserverStartMode::kOnDemand);
  if (!instance_) {
    return SandboxeeProcess();
  }
  SandboxeeProcess process =
      instance_->fork_client_.SendRequest(request, exec_fd, comms_fd);
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
  return process;
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
