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

// Implementation of the sandbox2::IPC class

#include "sandboxed_api/sandbox2/ipc.h"

#include <sys/socket.h>

#include <memory>
#include <thread>

#include "absl/log/log.h"
#include "sandboxed_api/sandbox2/logserver.h"
#include "sandboxed_api/sandbox2/logsink.h"
#include "sandboxed_api/util/raw_logging.h"

namespace sandbox2 {

void IPC::SetUpServerSideComms(int fd) { comms_ = std::make_unique<Comms>(fd); }

void IPC::MapFd(int local_fd, int remote_fd) {
  VLOG(3) << "Will send: " << local_fd << ", to overwrite: " << remote_fd;

  fd_map_.push_back(std::make_tuple(local_fd, remote_fd, ""));
}

int IPC::ReceiveFd(int remote_fd) { return ReceiveFd(remote_fd, ""); }

int IPC::ReceiveFd(absl::string_view name) { return ReceiveFd(-1, name); }

int IPC::ReceiveFd(int remote_fd, absl::string_view name) {
  int sv[2];
  if (socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) == -1) {
    PLOG(FATAL) << "socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)";
  }

  VLOG(3) << "Created a socketpair (" << sv[0] << "/" << sv[1] << "), "
          << "which will overwrite remote_fd: " << remote_fd;

  fd_map_.push_back(std::make_tuple(sv[1], remote_fd, std::string(name)));

  return sv[0];
}

bool IPC::SendFdsOverComms() {
  if (!(comms_->SendUint32(fd_map_.size()))) {
    LOG(ERROR) << "Couldn't send IPC fd size";
    return false;
  }

  for (const auto& fd_tuple : fd_map_) {
    if (!(comms_->SendInt32(std::get<1>(fd_tuple)))) {
      LOG(ERROR) << "SendInt32: Couldn't send " << std::get<1>(fd_tuple);
      return false;
    }
    if (!(comms_->SendFD(std::get<0>(fd_tuple)))) {
      LOG(ERROR) << "SendFd: Couldn't send " << std::get<0>(fd_tuple);
      return false;
    }

    if (!(comms_->SendString(std::get<2>(fd_tuple)))) {
      LOG(ERROR) << "SendString: Couldn't send " << std::get<2>(fd_tuple);
      return false;
    }

    VLOG(3) << "IPC: local_fd: " << std::get<0>(fd_tuple)
            << ", remote_fd: " << std::get<1>(fd_tuple) << " sent";
  }

  return true;
}

void IPC::InternalCleanupFdMap() {
  for (const auto& fd_tuple : fd_map_) {
    close(std::get<0>(fd_tuple));
  }
  fd_map_.clear();
}

void IPC::EnableLogServer() {
  int fd = ReceiveFd(LogSink::kLogFDName);
  auto logger = [fd] {
    LogServer log_server(fd);
    log_server.Run();
  };
  std::thread log_thread{logger};
  log_thread.detach();
}

}  // namespace sandbox2
