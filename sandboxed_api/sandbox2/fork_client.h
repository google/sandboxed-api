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

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

namespace sandbox2 {

// Envvar indicating that this process should not start the fork-server.
constexpr inline char kForkServerDisableEnv[] = "SANDBOX2_NOFORKSERVER";

class Comms;
class ForkRequest;

class ForkClient {
 public:
  ForkClient(pid_t pid, Comms* comms) : pid_(pid), comms_(comms) {}
  ForkClient(const ForkClient&) = delete;
  ForkClient& operator=(const ForkClient&) = delete;

  // Sends the fork request over the supplied Comms channel.
  pid_t SendRequest(const ForkRequest& request, int exec_fd, int comms_fd,
                    int user_ns_fd = -1, pid_t* init_pid = nullptr);

  pid_t pid() { return pid_; }

 private:
  // Pid of the ForkServer.
  pid_t pid_;
  // Comms channel connecting with the ForkServer. Not owned by the object.
  Comms* comms_ ABSL_GUARDED_BY(comms_mutex_);
  // Mutex locking transactions (requests) over the Comms channel.
  absl::Mutex comms_mutex_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_FORK_CLIENT_H_
