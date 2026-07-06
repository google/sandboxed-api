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

// StartGlobalForkServer() is called early in a process using a constructor, so
// it can fork() safely (in a single-threaded context)

#ifndef SANDBOXED_API_SANDBOX2_GLOBAL_FORKCLIENT_H_
#define SANDBOXED_API_SANDBOX2_GLOBAL_FORKCLIENT_H_

#include <sys/types.h>

#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/flags.h"  // IWYU pragma: export
#include "sandboxed_api/sandbox2/fork_client.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/util/fileops.h"

namespace sandbox2 {

class MonitorBase;

void DisableCompressStackDepot(google::protobuf::RepeatedPtrField<std::string>* envs);

class GlobalForkClient {
 public:
  GlobalForkClient(int fd, pid_t pid)
      : comms_(fd), fork_client_(pid, &comms_, /*is_global=*/true) {}

  static SandboxeeProcess SendRequest(const ForkRequest& request, int exec_fd,
                                      int comms_fd,
                                      ForkClient* fork_client = nullptr);
  static pid_t GetPid() { return GetGlobalData().GetPid(); }
  static void EnsureStarted() {
    GetGlobalData().EnsureStarted(GlobalForkserverStartMode::kOnDemand);
  }
  static void Shutdown() { GetGlobalData().Shutdown(); }
  static bool IsStarted() { return GetGlobalData().IsStarted(); }

 private:
  class GlobalData {
   public:
    GlobalData() = default;
    absl::StatusOr<ForkClient::PendingRequest> InitiateRequest(
        const ForkRequest& request) ABSL_LOCKS_EXCLUDED(mutex_);
    absl::Status SetupOptions(ForkClient::PendingRequest::Options& options,
                              const ForkRequest& request);
    void EnsureStarted(GlobalForkserverStartMode mode)
        ABSL_LOCKS_EXCLUDED(mutex_) {
      absl::MutexLock lock(mutex_);
      EnsureStartedLocked(mode);
    }
    pid_t GetPid() ABSL_LOCKS_EXCLUDED(mutex_);
    void Shutdown() ABSL_LOCKS_EXCLUDED(mutex_);
    bool IsStarted() ABSL_LOCKS_EXCLUDED(mutex_);
    void ForceStart() ABSL_LOCKS_EXCLUDED(mutex_);
   private:
    void EnsureStartedLocked(GlobalForkserverStartMode mode)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
    absl::Status SetupInitialNamespacesLocked()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
    absl::Status SetupSharedNetnsNamespacesLocked()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
    absl::Mutex mutex_;
    std::unique_ptr<GlobalForkClient> instance_ ABSL_GUARDED_BY(mutex_);
    sapi::file_util::fileops::FDCloser initial_userns_fd_
        ABSL_GUARDED_BY(mutex_);
    sapi::file_util::fileops::FDCloser initial_mntns_fd_
        ABSL_GUARDED_BY(mutex_);
    sapi::file_util::fileops::FDCloser shared_netns_fd_ ABSL_GUARDED_BY(mutex_);
  };
  friend void StartGlobalForkserverFromLibCtor();

  static GlobalData& GetGlobalData();

  Comms comms_;
  ForkClient fork_client_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_GLOBAL_FORKCLIENT_H_
