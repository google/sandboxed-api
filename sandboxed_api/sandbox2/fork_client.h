// Copyright 2020 Google LLC
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

#ifndef SANDBOXED_API_SANDBOX2_FORK_CLIENT_H_
#define SANDBOXED_API_SANDBOX2_FORK_CLIENT_H_

#include <sys/types.h>

#include <type_traits>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/util/fileops.h"

namespace sandbox2 {

struct SandboxeeProcess {
  pid_t init_pid = -1;
  pid_t main_pid = -1;
  sapi::file_util::fileops::FDCloser status_fd;
};

class ForkClient {
 public:
  class PendingRequest {
   public:
    struct Options {
      int comms_fd = -1;
      int exec_fd = -1;
      int initial_userns_fd = -1;
      int shared_pidns_fd = -1;
      int mntns_fd = -1;
      int shared_netns_fd = -1;
    };

    PendingRequest(PendingRequest&&) = default;
    PendingRequest& operator=(PendingRequest&&) = default;

    absl::StatusOr<SandboxeeProcess> Finalize(const Options& options) &&;

   private:
    friend class ForkClient;

    PendingRequest(Comms setup_comms, bool has_init_pid, bool needs_status_fd)
        : setup_comms_(std::move(setup_comms)),
          has_init_pid_(has_init_pid),
          needs_status_fd_(needs_status_fd) {}

    Comms setup_comms_;
    bool has_init_pid_;
    bool needs_status_fd_;
  };

  ForkClient(pid_t pid, Comms* comms) : ForkClient(pid, comms, false) {}

  ForkClient(const ForkClient&) = delete;
  ForkClient& operator=(const ForkClient&) = delete;

  ~ForkClient();

  // Runs a custom transaction over the Comms channel.
  template <typename F>
  typename std::invoke_result_t<F, Comms*> RunCommsTransaction(F&& func) {
    absl::MutexLock lock(comms_mutex_);
    return func(comms_);
  }

  pid_t pid() { return pid_; }

 private:

  friend class GlobalForkClient;

  ForkClient(pid_t pid, Comms* comms, bool is_global);

  // Initiates the fork request.
  absl::StatusOr<PendingRequest> InitiateRequest(const ForkRequest& request);

  // Sends an initialization request.
  // Returns the comms channel to the setup process.
  absl::StatusOr<Comms> SendInitializeRequest(
      ForkRequest::InitializationType init_type);

  absl::StatusOr<Comms> SendRequestAndReceiveSetupComms(
      const ForkRequest& request) ABSL_LOCKS_EXCLUDED(comms_mutex_);

  // Pid of the ForkServer.
  pid_t pid_;
  // Comms channel connecting with the ForkServer. Not owned by the object.
  Comms* comms_ ABSL_GUARDED_BY(comms_mutex_);
  // Is it the global forkserver
  bool is_global_;
  // Mutex locking transactions (requests) over the Comms channel.
  absl::Mutex comms_mutex_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_FORK_CLIENT_H_
