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

// StartGlobalForkServer() is called early in a process using a constructor, so
// it can fork() safely (in a single-threaded context)

#ifndef SANDBOXED_API_SANDBOX2_GLOBAL_FORKCLIENT_H_
#define SANDBOXED_API_SANDBOX2_GLOBAL_FORKCLIENT_H_

#include <sys/types.h>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/fork_client.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"

namespace sandbox2 {

class GlobalForkClient {
 public:
  GlobalForkClient(int fd, pid_t pid)
      : comms_(fd), fork_client_(pid, &comms_) {}

  static pid_t SendRequest(const ForkRequest& request, int exec_fd,
                           int comms_fd, int user_ns_fd = -1,
                           pid_t* init_pid = nullptr)
      ABSL_LOCKS_EXCLUDED(instance_mutex_);
  static pid_t GetPid() ABSL_LOCKS_EXCLUDED(instance_mutex_);

  static void EnsureStarted() ABSL_LOCKS_EXCLUDED(instance_mutex_);
  static void Shutdown() ABSL_LOCKS_EXCLUDED(instance_mutex_);

 private:
  static absl::Mutex instance_mutex_;
  static GlobalForkClient* instance_ ABSL_GUARDED_BY(instance_mutex_);

  static void EnsureStartedLocked(bool start_if_needed = true)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(instance_mutex_);

  Comms comms_;
  ForkClient fork_client_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_GLOBAL_FORKCLIENT_H_
