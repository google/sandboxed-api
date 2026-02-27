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

#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
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

absl::StatusOr<ForkClient::PendingRequest> ForkClient::InitiateRequest(
    const ForkRequest& request, int exec_fd, int comms_fd) {
  int raw_setup_fd = -1;
  // Acquire the channel ownership for this request (transaction).

  absl::MutexLock l(comms_mutex_);

  if (!comms_->SendProtoBuf(request)) {
    return absl::InternalError("Sending PB to the ForkServer failed");
  }
  CHECK(comms_fd != -1) << "comms_fd was not properly set up";
  if (!comms_->SendFD(comms_fd)) {
    return absl::InternalError(absl::StrCat("Sending Comms FD (", comms_fd,
                                            ") to the ForkServer failed"));
  }
  if (request.mode() == FORKSERVER_FORK_EXECVE ||
      request.mode() == FORKSERVER_FORK_EXECVE_SANDBOX) {
    CHECK(exec_fd != -1) << "exec_fd cannot be -1 in execve mode";
    if (!comms_->SendFD(exec_fd)) {
      return absl::InternalError(absl::StrCat("Sending Exec FD (", exec_fd,
                                              ") to the ForkServer failed"));
    }
  }

  if (!comms_->RecvFD(&raw_setup_fd)) {
    return absl::InternalError("Receiving setup fd from the ForkServer failed");
  }

  return PendingRequest(Comms(raw_setup_fd),
                        request.clone_flags() & CLONE_NEWPID,
                        request.monitor_type() == FORKSERVER_MONITOR_UNOTIFY);
}

SandboxeeProcess ForkClient::SendRequest(const ForkRequest& request,
                                         int exec_fd, int comms_fd) {
  absl::StatusOr<PendingRequest> pending_request =
      InitiateRequest(request, exec_fd, comms_fd);
  if (!pending_request.ok()) {
    LOG(ERROR) << pending_request.status();
    return SandboxeeProcess();
  }
  absl::StatusOr<SandboxeeProcess> result =
      std::move(*pending_request).Finalize();
  if (!result.ok()) {
    LOG(ERROR) << result.status();
    return SandboxeeProcess();
  }
  return *std::move(result);
}

absl::StatusOr<SandboxeeProcess> ForkClient::PendingRequest::Finalize() && {
  SandboxeeProcess process;
  if (needs_status_fd_) {
    int fd = -1;
    if (!setup_comms_.RecvFD(&fd)) {
      return absl::InternalError(
          "Receiving status fd from the ForkServer failed");
    }
    process.status_fd = FDCloser(fd);
  }

  if (has_init_pid_) {
    // Receive init process ID.
    if (!setup_comms_.RecvCreds(&process.init_pid, nullptr, nullptr)) {
      return absl::InternalError(
          "Receiving init PID from the ForkServer failed");
    }
  }

  // Receive sandboxee process ID.
  if (!setup_comms_.RecvCreds(&process.main_pid, nullptr, nullptr)) {
    return absl::InternalError(
        "Receiving sandboxee PID from the ForkServer failed");
  }
  return process;
}

}  // namespace sandbox2
