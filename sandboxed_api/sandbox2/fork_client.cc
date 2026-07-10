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

#include <string>
#include <utility>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/flags.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/sandbox2/setup_latency_breakdown.h"
#include "sandboxed_api/util/fileops.h"

namespace sandbox2 {
namespace {
using ::sapi::file_util::fileops::FDCloser;

void ReportLatency(const SetupLatencyBreakdown& latency_breakdown) {
  if (!absl::GetFlag(FLAGS_sandbox2_log_setup_latency_breakdown)) {
    return;
  }
  LOG(INFO) << "Setup latency breakdown:";
  for (int i = 0; i < SetupLatencyBreakdown::kNumSetupSteps; ++i) {
    LOG(INFO) << "  "
              << SetupLatencyBreakdown::SetupStepToString(
                     static_cast<SetupLatencyBreakdown::SetupStep>(i))
              << ": "
              << latency_breakdown.GetLatency(
                     static_cast<SetupLatencyBreakdown::SetupStep>(i));
  }
}
}  // namespace

ForkClient::ForkClient(pid_t pid, Comms* comms, bool is_global)
    : pid_(pid), comms_(comms), is_global_(is_global) {
}

ForkClient::~ForkClient() {
}

absl::StatusOr<Comms> ForkClient::SendRequestAndReceiveSetupComms(
    const ForkRequest& request) {
  int raw_setup_fd = -1;
  // Acquire the channel ownership for this request (transaction).

  absl::MutexLock l(comms_mutex_);

  if (!comms_->SendProtoBuf(request)) {
    return absl::InternalError("Sending PB to the ForkServer failed");
  }

  if (!comms_->RecvFD(&raw_setup_fd)) {
    return absl::InternalError("Receiving setup fd from the ForkServer failed");
  }

  return Comms(raw_setup_fd);
}

absl::StatusOr<ForkClient::PendingRequest> ForkClient::InitiateRequest(
    const ForkRequest& request) {
  ABSL_ASSIGN_OR_RETURN(Comms setup_comms,
                        SendRequestAndReceiveSetupComms(request));

  // TODO(cffsmith): Once we add support for UnotifyMonitor in Landlock mode, We
  // need to check here, as we will have an init process in that case even
  // though we don't have CLONE_NEWPID.
  return PendingRequest(std::move(setup_comms),
                        request.clone_flags() & CLONE_NEWPID,
                        request.monitor_type() == FORKSERVER_MONITOR_UNOTIFY);
}

absl::StatusOr<Comms> ForkClient::SendInitializeRequest(
    ForkRequest::InitializationType init_type) {
  ForkRequest request;
  request.set_mode(FORKSERVER_INITIALIZE);
  request.set_initialization_type(init_type);
  return SendRequestAndReceiveSetupComms(request);
}

absl::StatusOr<SandboxeeProcess> ForkClient::PendingRequest::Finalize(
    const ForkClient::PendingRequest::Options& options) && {
  SandboxeeProcess process;
  CHECK(options.comms_fd != -1) << "comms_fd was not properly set up";
  if (!setup_comms_.SendFD(options.comms_fd)) {
    return absl::InternalError(absl::StrCat(
        "Sending Comms FD (", options.comms_fd, ") to the ForkServer failed"));
  }
  if (options.exec_fd != -1 && !setup_comms_.SendFD(options.exec_fd)) {
    return absl::InternalError(absl::StrCat(
        "Sending Exec FD (", options.exec_fd, ") to the ForkServer failed"));
  }
  if (options.initial_userns_fd != -1 &&
      !setup_comms_.SendFD(options.initial_userns_fd)) {
    return absl::InternalError(absl::StrCat("Sending initial userns FD (",
                                            options.initial_userns_fd,
                                            ") to the ForkServer failed"));
  }

  if (options.initial_mntns_fd != -1 &&
      !setup_comms_.SendFD(options.initial_mntns_fd)) {
    return absl::InternalError(absl::StrCat("Sending initial mntns FD (",
                                            options.initial_mntns_fd,
                                            ") to the ForkServer failed"));
  }

  if (options.shared_pidns_mntns_fd != -1 &&
      !setup_comms_.SendFD(options.shared_pidns_mntns_fd)) {
    return absl::InternalError(absl::StrCat("Sending shared pidns mntns FD (",
                                            options.shared_pidns_mntns_fd,
                                            ") to the ForkServer failed"));
  }

  if (options.shared_pidns_fd != -1 &&
      !setup_comms_.SendFD(options.shared_pidns_fd)) {
    return absl::InternalError(absl::StrCat("Sending shared pidns FD (",
                                            options.shared_pidns_fd,
                                            ") to the ForkServer failed"));
  }

  if (options.shared_netns_fd != -1 &&
      !setup_comms_.SendFD(options.shared_netns_fd)) {
    return absl::InternalError(absl::StrCat("Sending shared netns FD (",
                                            options.shared_netns_fd,
                                            ") to the ForkServer failed"));
  }

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

  // Receive setup latency tracer data and report it.
  if (SetupLatencyBreakdown latency_breakdown;
      latency_breakdown.Receive(setup_comms_)) {
    ReportLatency(latency_breakdown);
  } else {
    LOG(WARNING) << "Failed to receive setup latency tracer data";
  }

  return process;
}

}  // namespace sandbox2
