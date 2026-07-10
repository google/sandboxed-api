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
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/no_destructor.h"
#include "absl/base/thread_annotations.h"
#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "google/protobuf/repeated_ptr_field.h"
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
  const char* const* envp;
};

template <typename C, typename AddFn>
void DisableCompressStackDepotImpl(C& envs, AddFn&& add_env) {
  auto disable_compress_stack_depot = [&envs,
                                       &add_env](absl::string_view sanitizer) {
    auto prefix = absl::StrCat(sanitizer, "_OPTIONS=");
    constexpr absl::string_view option = "compress_stack_depot=0";
    auto it = absl::c_find_if(envs, [&prefix](const std::string& env) {
      return absl::StartsWith(env, prefix);
    });
    if (it != envs.end()) {
      // If it's already there, the last value will be used.
      absl::StrAppend(&*it, ":", option);
      return;
    }
    add_env(absl::StrCat(prefix, option));
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
  util::Execveat(args->exec_fd, "", argv, args->envp, AT_EMPTY_PATH);
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
  constexpr size_t kStackSize = util::kPthreadStackMin;
  int clone_flags = CLONE_VM | CLONE_VFORK | SIGCHLD;
  // CLONE_VM does not play well with TSan.
  if constexpr (sapi::sanitizers::IsTSan()) {
    clone_flags &= ~CLONE_VM & ~CLONE_VFORK;
  }
  char* stack =
      static_cast<char*>(mmap(NULL, kStackSize, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0));
  if (stack == MAP_FAILED) {
    return absl::ErrnoToStatus(errno, "Allocating stack failed");
  }
  absl::Cleanup stack_dealloc = [stack] { munmap(stack, kStackSize); };
  std::vector<std::string> env = util::CharPtrArray(environ).ToStringVector();
  DisableCompressStackDepotImpl(env, [&env](absl::string_view value) {
    env.push_back(std::string(value));
  });
  util::CharPtrArray envp = util::CharPtrArray::FromStringVector(env);
  ForkserverArgs args = {
      .exec_fd = exec_fd,
      .comms_fd = sv[0],
      .envp = envp.data(),
  };
  pid_t pid = clone(LaunchForkserver, &stack[kStackSize], clone_flags, &args,
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

GlobalForkClient::GlobalData& GlobalForkClient::GetGlobalData() {
  static absl::NoDestructor<GlobalForkClient::GlobalData> global_data;
  return *global_data;
}

void DisableCompressStackDepot(google::protobuf::RepeatedPtrField<std::string>* envs) {
  DisableCompressStackDepotImpl(
      *envs, [&envs](absl::string_view env) { envs->Add(std::string(env)); });
}

void GlobalForkClient::GlobalData::EnsureStartedLocked(
    GlobalForkserverStartMode mode) {
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
  instance_ = *std::move(forkserver);
}

void GlobalForkClient::GlobalData::ForceStart() {
  absl::MutexLock lock(mutex_);
  SAPI_RAW_CHECK(instance_ == nullptr,
                 "A force start requested when the Global Fork-Server was "
                 "already running");
  absl::StatusOr<std::unique_ptr<GlobalForkClient>> forkserver =
      StartGlobalForkServer();
  SAPI_RAW_CHECK(forkserver.ok(), forkserver.status().ToString().c_str());
  instance_ = *std::move(forkserver);
}

void GlobalForkClient::GlobalData::CloseNamespacesLocked() {
  initial_userns_fd_.Close();
  initial_mntns_fd_.Close();
  shared_netns_fd_.Close();
  shared_pidns_mntns_fd_.Close();
  shared_pidns_fd_.Close();
}

void GlobalForkClient::GlobalData::Shutdown() {
  pid_t pid = -1;
  {
    absl::MutexLock lock(mutex_);
    if (instance_) {
      pid = instance_->fork_client_.pid();
    }
    instance_.reset();
    CloseNamespacesLocked();
  }
  if (pid != -1) {
    WaitForForkserver(pid);
  }
}

absl::StatusOr<ForkClient::PendingRequest>
GlobalForkClient::GlobalData::InitiateRequest(const ForkRequest& request) {
  absl::StatusOr<ForkClient::PendingRequest> pending_request;
  {
    absl::ReleasableMutexLock lock(mutex_);
    EnsureStartedLocked(GlobalForkserverStartMode::kOnDemand);
    if (!instance_) {
      return absl::InternalError("Global forkserver not started");
    }
    pending_request = instance_->fork_client_.InitiateRequest(request);
    if (instance_->comms_.IsTerminated()) {
      LOG(ERROR) << "Global forkserver connection terminated";
      pid_t server_pid = instance_->fork_client_.pid();
      instance_.reset();
      // Don't wait for process exit while still holding the lock and
      // potentially blocking other threads.
      lock.Release();
      WaitForForkserver(server_pid);
    }
  }
  return pending_request;
}

absl::Status GlobalForkClient::GlobalData::SetupInitialNamespacesLocked() {
  ABSL_ASSIGN_OR_RETURN(Comms setup_comms,
                        instance_->fork_client_.SendInitializeRequest(
                            ForkRequest::INITIALIZE_INITIAL_NAMESPACES));

  file_util::fileops::FDCloser userns_fd;
  if (!setup_comms.RecvFD(&userns_fd)) {
    return absl::InternalError("Receiving initial user namespace fd failed");
  }
  file_util::fileops::FDCloser mntns_fd;
  if (!setup_comms.RecvFD(&mntns_fd)) {
    return absl::InternalError("Receiving initial mount namespace fd failed");
  }
  initial_userns_fd_ = std::move(userns_fd);
  initial_mntns_fd_ = std::move(mntns_fd);
  return absl::OkStatus();
}

absl::Status GlobalForkClient::GlobalData::SetupSharedPidNamespacesLocked() {
  if (initial_userns_fd_.get() == -1) {
    ABSL_RETURN_IF_ERROR(SetupInitialNamespacesLocked());
  }

  ABSL_ASSIGN_OR_RETURN(Comms setup_comms,
                        instance_->fork_client_.SendInitializeRequest(
                            ForkRequest::INITIALIZE_SHARED_PID_NAMESPACES));

  if (!setup_comms.SendFD(initial_userns_fd_.get())) {
    return absl::InternalError(
        "Sending initial user namespace fd for shared pidns failed");
  }
  file_util::fileops::FDCloser mntns_fd;
  if (!setup_comms.RecvFD(&mntns_fd)) {
    return absl::InternalError("Receiving shared pidns mount fd failed");
  }
  file_util::fileops::FDCloser pidns_fd;
  if (!setup_comms.RecvFD(&pidns_fd)) {
    return absl::InternalError("Receiving shared pidns fd failed");
  }
  shared_pidns_mntns_fd_ = std::move(mntns_fd);
  shared_pidns_fd_ = std::move(pidns_fd);
  return absl::OkStatus();
}

absl::Status GlobalForkClient::GlobalData::SetupSharedNetnsNamespacesLocked() {
  if (initial_userns_fd_.get() == -1) {
    ABSL_RETURN_IF_ERROR(SetupInitialNamespacesLocked());
  }
  ABSL_ASSIGN_OR_RETURN(Comms setup_comms,
                        instance_->fork_client_.SendInitializeRequest(
                            ForkRequest::INITIALIZE_EMPTY_NETNS));
  if (!setup_comms.SendFD(initial_userns_fd_.get())) {
    return absl::InternalError(
        "Sending user namespace fd for empty netns failed");
  }
  file_util::fileops::FDCloser netns_fd;
  if (!setup_comms.RecvFD(&netns_fd)) {
    return absl::InternalError("Receiving empty netns fd failed");
  }
  shared_netns_fd_ = std::move(netns_fd);
  return absl::OkStatus();
}

absl::Status GlobalForkClient::GlobalData::SetupOptions(
    ForkClient::PendingRequest::Options& options, const ForkRequest& request) {
  if (!(request.clone_flags() & CLONE_NEWUSER)) {
    return absl::OkStatus();
  }
  absl::MutexLock lock(mutex_);
  if (initial_userns_fd_.get() == -1) {
    ABSL_RETURN_IF_ERROR(SetupInitialNamespacesLocked());
  }
  if (request.use_landlock()) {
    if (shared_pidns_mntns_fd_.get() == -1) {
      ABSL_RETURN_IF_ERROR(SetupSharedPidNamespacesLocked());
    }
    SAPI_RAW_CHECK(shared_pidns_mntns_fd_.get() != -1,
                   "Shared pidns mntns fd not initialized");
    SAPI_RAW_CHECK(shared_pidns_fd_.get() != -1,
                   "Shared pidns fd not initialized");
    options.initial_userns_fd = initial_userns_fd_.get();
    options.shared_pidns_mntns_fd = shared_pidns_mntns_fd_.get();
    options.shared_pidns_fd = shared_pidns_fd_.get();
  } else {
    SAPI_RAW_CHECK(initial_mntns_fd_.get() != -1,
                   "Initial mntns fd not initialized");
    // The FDs are never closed after the initialization, so it is fine to use
    // them outside of the lock. The lock guards just the initialization.
    options.initial_userns_fd = initial_userns_fd_.get();
    options.initial_mntns_fd = initial_mntns_fd_.get();
  }
  if (request.netns_mode() == NETNS_MODE_SHARED_PER_FORKSERVER) {
    if (shared_netns_fd_.get() == -1) {
      ABSL_RETURN_IF_ERROR(SetupSharedNetnsNamespacesLocked());
    }
    options.shared_netns_fd = shared_netns_fd_.get();
  }
  return absl::OkStatus();
}

SandboxeeProcess GlobalForkClient::SendRequest(const ForkRequest& request,
                                               int exec_fd, int comms_fd,
                                               ForkClient* fork_client) {
  absl::StatusOr<ForkClient::PendingRequest> pending_request =
      fork_client != nullptr ? fork_client->InitiateRequest(request)
                             : GetGlobalData().InitiateRequest(request);
  if (!pending_request.ok()) {
    LOG(ERROR) << "InitiateRequest failed: "
               << pending_request.status().message();
    return SandboxeeProcess();
  }
  ForkClient::PendingRequest::Options options;
  options.exec_fd = exec_fd;
  options.comms_fd = comms_fd;
  absl::Status status = GetGlobalData().SetupOptions(options, request);
  if (!status.ok()) {
    LOG(ERROR) << "SetupOptions failed: " << status.message();
    return SandboxeeProcess();
  }
  absl::StatusOr<SandboxeeProcess> sandboxee_process =
      std::move(*pending_request).Finalize(options);
  if (!sandboxee_process.ok()) {
    LOG(ERROR) << "Finalize failed: " << sandboxee_process.status().message();
    return SandboxeeProcess();
  }
  return *std::move(sandboxee_process);
}

pid_t GlobalForkClient::GlobalData::GetPid() {
  absl::MutexLock lock(mutex_);
  EnsureStartedLocked(GlobalForkserverStartMode::kOnDemand);
  if (!instance_) {
    return -1;
  }
  return instance_->fork_client_.pid();
}

bool GlobalForkClient::GlobalData::IsStarted() {
  absl::ReaderMutexLock lock(mutex_);
  return instance_ != nullptr;
}
}  // namespace sandbox2
