// Copyright 2020 Google LLC
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

#include "sandboxed_api/sandbox2/fork_client.h"

#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/util/raw_logging.h"

namespace sandbox2 {

pid_t ForkClient::SendRequest(const ForkRequest& request, int exec_fd,
                              int comms_fd, int user_ns_fd, pid_t* init_pid) {
  // Acquire the channel ownership for this request (transaction).
  absl::MutexLock l(&comms_mutex_);

  if (!comms_->SendProtoBuf(request)) {
    SAPI_RAW_LOG(ERROR, "Sending PB to the ForkServer failed");
    return -1;
  }
  SAPI_RAW_CHECK(comms_fd != -1, "comms_fd was not properly set up");
  if (!comms_->SendFD(comms_fd)) {
    SAPI_RAW_LOG(ERROR, "Sending Comms FD (%d) to the ForkServer failed",
                 comms_fd);
    return -1;
  }
  if (request.mode() == FORKSERVER_FORK_EXECVE ||
      request.mode() == FORKSERVER_FORK_EXECVE_SANDBOX) {
    SAPI_RAW_CHECK(exec_fd != -1, "exec_fd cannot be -1 in execve mode");
    if (!comms_->SendFD(exec_fd)) {
      SAPI_RAW_LOG(ERROR, "Sending Exec FD (%d) to the ForkServer failed",
                   exec_fd);
      return -1;
    }
  }

  if (request.mode() == FORKSERVER_FORK_JOIN_SANDBOX_UNWIND) {
    SAPI_RAW_CHECK(user_ns_fd != -1, "user_ns_fd cannot be -1 in unwind mode");
    if (!comms_->SendFD(user_ns_fd)) {
      SAPI_RAW_LOG(ERROR, "Sending user ns FD (%d) to the ForkServer failed",
                   user_ns_fd);
      return -1;
    }
  }

  int32_t pid;
  // Receive init process ID.
  if (!comms_->RecvInt32(&pid)) {
    SAPI_RAW_LOG(ERROR, "Receiving init PID from the ForkServer failed");
    return -1;
  }
  if (init_pid) {
    *init_pid = static_cast<pid_t>(pid);
  }

  // Receive sandboxee process ID.
  if (!comms_->RecvInt32(&pid)) {
    SAPI_RAW_LOG(ERROR, "Receiving sandboxee PID from the ForkServer failed");
    return -1;
  }
  return static_cast<pid_t>(pid);
}

}  // namespace sandbox2
