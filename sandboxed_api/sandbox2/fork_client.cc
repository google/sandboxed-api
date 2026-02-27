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

#include "sandboxed_api/sandbox2/fork_client.h"

#include <sys/types.h>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/util/fileops.h"

namespace sandbox2 {

using ::sapi::file_util::fileops::FDCloser;

ForkClient::ForkClient(pid_t pid, Comms* comms, bool is_global)
    : pid_(pid), comms_(comms), is_global_(is_global) {
}

ForkClient::~ForkClient() {
}

SandboxeeProcess ForkClient::SendRequest(const ForkRequest& request,
                                         int exec_fd, int comms_fd) {
  SandboxeeProcess process;
  int raw_setup_fd = -1;
  // Acquire the channel ownership for this request (transaction).
  {
    absl::MutexLock l(comms_mutex_);

    if (!comms_->SendProtoBuf(request)) {
      LOG(ERROR) << "Sending PB to the ForkServer failed";
      return process;
    }
    CHECK(comms_fd != -1) << "comms_fd was not properly set up";
    if (!comms_->SendFD(comms_fd)) {
      LOG(ERROR) << "Sending Comms FD (" << comms_fd
                 << ") to the ForkServer failed";
      return process;
    }
    if (request.mode() == FORKSERVER_FORK_EXECVE ||
        request.mode() == FORKSERVER_FORK_EXECVE_SANDBOX) {
      CHECK(exec_fd != -1) << "exec_fd cannot be -1 in execve mode";
      if (!comms_->SendFD(exec_fd)) {
        LOG(ERROR) << "Sending Exec FD (" << exec_fd
                   << ") to the ForkServer failed";
        return process;
      }
    }

    if (!comms_->RecvFD(&raw_setup_fd)) {
      LOG(ERROR) << "Receiving setup fd from the ForkServer failed";
      return process;
    }
  }

  // Coordinate rest of the setup via a new comms channel.
  Comms setup_comms(raw_setup_fd);

  if (request.monitor_type() == FORKSERVER_MONITOR_UNOTIFY) {
    int fd = -1;
    if (!setup_comms.RecvFD(&fd)) {
      LOG(ERROR) << "Receiving status fd from the ForkServer failed";
      return process;
    }
    process.status_fd = FDCloser(fd);
  }

  if (request.clone_flags() & CLONE_NEWPID) {
    // Receive init process ID.
    if (!setup_comms.RecvCreds(&process.init_pid, nullptr, nullptr)) {
      LOG(ERROR) << "Receiving init PID from the ForkServer failed";
      return process;
    }
  }

  // Receive sandboxee process ID.
  if (!setup_comms.RecvCreds(&process.main_pid, nullptr, nullptr)) {
    LOG(ERROR) << "Receiving sandboxee PID from the ForkServer failed";
    return process;
  }
  return process;
}

}  // namespace sandbox2
