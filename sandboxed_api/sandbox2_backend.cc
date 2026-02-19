// Copyright 2026 Google LLC
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

#include "sandboxed_api/sandbox2_backend.h"

#include <sys/mman.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "sandboxed_api/file_toc.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "sandboxed_api/embed_file.h"
#include "sandboxed_api/sandbox.h"
#include "sandboxed_api/sandbox2/buffer.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/fork_client.h"
#include "sandboxed_api/sandbox2/notify.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2_rpcchannel.h"
#include "sandboxed_api/sandbox_config.h"
#include "sandboxed_api/shared_memory_rpcchannel.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/runfiles.h"
#include "sandboxed_api/util/status_macros.h"

namespace sapi {

Sandbox2Backend::Sandbox2Backend(SandboxBase* sandbox_base,
                                 SandboxConfig config)
    : config_(std::move(config)), sandbox_base_(sandbox_base) {
  CHECK(config_.sandbox2.fork_client_context.has_value());
}

Sandbox2Backend::~Sandbox2Backend() {
  Terminate();
  // The forkserver will die automatically when the executor goes out of scope
  // and closes the comms object.
}

void Sandbox2Backend::Terminate(bool attempt_graceful_exit) {
  if (!is_active()) {
    return;
  }

  absl::StatusOr<sandbox2::Result> result;
  if (attempt_graceful_exit) {
    if (absl::Status requested_exit = rpc_channel_->Exit();
        !requested_exit.ok()) {
      LOG(WARNING)
          << "rpc_channel->Exit() failed, calling AwaitResultWithTimeout(1) "
          << requested_exit;
    }
    result = s2_->AwaitResultWithTimeout(absl::Seconds(1));
    if (!result.ok()) {
      LOG(WARNING) << "s2_->AwaitResultWithTimeout failed, status: "
                   << result.status() << " Killing PID: " << pid();
    }
  }

  if (!attempt_graceful_exit || !result.ok()) {
    s2_->Kill();
    result = s2_->AwaitResult();
  }

  if ((result->final_status() == sandbox2::Result::OK &&
       result->reason_code() == 0) ||
      (!attempt_graceful_exit &&
       result->final_status() == sandbox2::Result::EXTERNAL_KILL)) {
    VLOG(2) << "Sandbox2 finished with: " << result->ToString();
  } else {
    LOG(WARNING) << "Sandbox2 finished with: " << result->ToString();
  }
}

std::unique_ptr<sandbox2::Notify> Sandbox2Backend::CreateNotifier() {
  return sandbox_base_->CreateNotifier();
}

static std::string PathToSAPILib(const std::string& lib_path) {
  return file::IsAbsolutePath(lib_path) ? lib_path
                                        : GetDataDependencyFilePath(lib_path);
}

void Sandbox2Backend::ApplySandbox2Config(sandbox2::Executor* executor) const {
  const Sandbox2Config& config = config_.sandbox2;
  if (config.enable_log_server) {
    executor->ipc()->EnableLogServer();
  }
  if (config.cwd.has_value()) {
    executor->set_cwd(*config.cwd);
  }
  if (config.limits.has_value()) {
    *executor->limits() = *config.limits;
  }
}

void Sandbox2Backend::MapFileDescriptors(sandbox2::Executor* executor) const {
  if (!config_.fd_mappings.has_value()) {
    return;
  }
  for (const auto& [host_fd, sandbox_fd] : *config_.fd_mappings) {
    executor->ipc()->MapDupedFd(host_fd.get(), sandbox_fd);
  }
}

absl::Status Sandbox2Backend::Init() {
  // It's already initialized
  if (is_active()) {
    return absl::OkStatus();
  }

  std::shared_ptr<sandbox2::Executor> fork_client_executor;
  std::shared_ptr<sandbox2::ForkClient> fork_client;
  {
    absl::MutexLock lock(fork_client_shared().mu_);
    // Initialize the forkserver if it is not already running.
    if (!fork_client_shared().client_) {
      auto sandboxee_source = fork_client_context().sandboxee_source_;

      std::string lib_path;
      int embed_lib_fd = -1;
      if (std::holds_alternative<const FileToc*>(sandboxee_source)) {
        const FileToc* embed_lib_toc =
            std::get<const FileToc*>(sandboxee_source);
        embed_lib_fd = EmbedFile::instance()->GetDupFdForFileToc(embed_lib_toc);
        if (embed_lib_fd == -1) {
          PLOG(ERROR) << "Cannot create executable FD for TOC:'"
                      << embed_lib_toc->name << "'";
          return absl::UnavailableError("Could not create executable FD");
        }
        lib_path = embed_lib_toc->name;
      } else {
        lib_path = PathToSAPILib(std::get<std::string>(sandboxee_source));
        if (lib_path.empty()) {
          LOG(ERROR) << "SAPI library path is empty";
          return absl::FailedPreconditionError("No SAPI library path given");
        }
      }
      std::vector<std::string> args = {lib_path};
      // Additional arguments, if needed.
      auto flags =
          config_.command_line_flags.value_or(SandboxConfig::DefaultFlags());
      for (const auto& [key, value] : flags) {
        args.push_back(absl::StrCat("--", key, "=", value));
      }

      fork_client_shared().executor_ =
          (embed_lib_fd >= 0) ? std::make_shared<sandbox2::Executor>(
                                    embed_lib_fd, args, EnvironmentVariables())
                              : std::make_shared<sandbox2::Executor>(
                                    lib_path, args, EnvironmentVariables());

      fork_client_shared().client_ =
          fork_client_shared().executor_->StartForkServer();

      if (!fork_client_shared().client_) {
        LOG(ERROR) << "Could not start forkserver";
        return absl::UnavailableError("Could not start the forkserver");
      }
    }
    fork_client_executor = fork_client_shared().executor_;
    fork_client = fork_client_shared().client_;
  }

  std::unique_ptr<sandbox2::Policy> s2p;
  if (config_.sandbox2.policy) {
    s2p = std::make_unique<sandbox2::Policy>(*config_.sandbox2.policy);
  } else {
    sandbox2::PolicyBuilder policy_builder =
        Sandbox2Config::DefaultPolicyBuilder();
    if (config_.sandbox2.use_unotify_monitor) {
      policy_builder.CollectStacktracesOnSignal(false);
    }
    s2p = policy_builder.BuildOrDie();
  }

  // Spawn new process from the forkserver.
  auto executor = std::make_unique<sandbox2::Executor>(fork_client.get());

  executor
      // The client.cc code is capable of enabling sandboxing on its own.
      ->set_enable_sandbox_before_exec(false)
      // By default, set cwd to "/", can be changed in ModifyExecutor().
      .set_cwd("/");
  // Disable time limits.
  *executor->limits() = Sandbox2Config::DefaultLimits();

  // Modify the executor, e.g. by setting custom limits and IPC.
  ApplySandbox2Config(executor.get());
  MapFileDescriptors(executor.get());

  s2_ = std::make_unique<sandbox2::Sandbox2>(std::move(executor),
                                             std::move(s2p), CreateNotifier());
  const sandbox2::Buffer* shared_memory_mapping = nullptr;
  if (config_.sandbox2.enable_shared_memory) {
    SAPI_ASSIGN_OR_RETURN(shared_memory_mapping,
                          s2_->CreateSharedMemoryMapping());
  }
  if (config_.sandbox2.use_unotify_monitor) {
    SAPI_RETURN_IF_ERROR(s2_->EnableUnotifyMonitor());
  }
  s2_awaited_ = false;
  auto res = s2_->RunAsync();

  comms_ = s2_->comms();
  pid_ = s2_->pid();

  rpc_channel_ = std::make_unique<Sandbox2RPCChannel>(comms_, pid_);
  if (config_.sandbox2.enable_shared_memory) {
    uint64_t remote_base_address;
    comms_->RecvUint64(&remote_base_address);
    void* shared_memory_local_ptr = shared_memory_mapping->data();
    auto shared_memory_remote_ptr =
        reinterpret_cast<void*>(remote_base_address);
    rpc_channel_ = std::make_unique<SharedMemoryRPCChannel>(
        std::move(rpc_channel_), shared_memory_mapping->size(),
        shared_memory_local_ptr, shared_memory_remote_ptr);
  }

  if (!res) {
    // Allow recovering from a bad fork client state.
    {
      absl::MutexLock lock(fork_client_shared().mu_);
      fork_client_shared().client_.reset();
    }
    sandbox2::Result result = s2_->AwaitResult();
    LOG(ERROR) << "Could not start the sandbox: " << result.ToString();
    return absl::UnavailableError(
        absl::StrCat("Could not start the sandbox: ", result.ToString()));
  }
  return absl::OkStatus();
}

bool Sandbox2Backend::is_active() const { return s2_ && !s2_->IsTerminated(); }

absl::StatusOr<int> Sandbox2Backend::GetPid() const {
  if (!is_active() || pid_ < 0) {
    return absl::UnavailableError("Sandbox not active");
  }
  return pid_;
}

const sandbox2::Result& Sandbox2Backend::AwaitResult() {
  if (s2_ && !s2_awaited_) {
    result_ = s2_->AwaitResult();
    s2_awaited_ = true;
  }
  return result_;
}

absl::Status Sandbox2Backend::SetWallTimeLimit(absl::Duration limit) const {
  if (!is_active()) {
    return absl::UnavailableError("Sandbox not active");
  }
  s2_->set_walltime_limit(limit);
  return absl::OkStatus();
}

}  // namespace sapi
