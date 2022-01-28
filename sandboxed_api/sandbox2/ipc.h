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

// The sandbox2::IPC class provides routines for exchanging data between sandbox
// and the sandboxee.

#ifndef SANDBOXED_API_SANDBOX2_IPC_H_
#define SANDBOXED_API_SANDBOX2_IPC_H_

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/comms.h"

namespace sandbox2 {

class IPC final {
 public:
  IPC() = default;

  IPC(const IPC&) = delete;
  IPC& operator=(const IPC&) = delete;

  ~IPC() { InternalCleanupFdMap(); }

  ABSL_DEPRECATED("Use Sandbox2::comms() instead")
  Comms* comms() const { return comms_.get(); }

  // Marks local_fd so that it should be sent to the remote process (sandboxee),
  // and duplicated onto remote_fd in it. The local_fd will be closed after
  // being sent (in SendFdsOverComms which is called by the Monitor class), so
  // it should not be used from that point on.
  void MapFd(int local_fd, int remote_fd);

  // Creates and returns a socketpair endpoint. The other endpoint of the
  // socketpair is marked as to be sent to the remote process (sandboxee) with
  // SendFdsOverComms() as with MapFd().
  // If a name is specified, uses the Client::GetMappedFD api to retrieve the
  // corresponding file descriptor in the sandboxee.
  int ReceiveFd(int remote_fd, absl::string_view name);
  int ReceiveFd(int remote_fd);
  int ReceiveFd(absl::string_view name);

  // Enable sandboxee logging, this will start a thread that waits for log
  // messages from the sandboxee. You'll also have to call
  // Client::SendLogsToSupervisor in the sandboxee.
  void EnableLogServer();

 private:
  friend class Executor;
  friend class Monitor;
  friend class IpcPeer;  // For testing

  // Uses a pre-connected file descriptor.
  void SetUpServerSideComms(int fd);

  // Sends file descriptors to the sandboxee. Close the local FDs (e.g. passed
  // in MapFd()) - they cannot be used anymore.
  bool SendFdsOverComms();

  void InternalCleanupFdMap();

  // Tuple of file descriptor pairs which will be sent to the sandboxee: in the
  // form of tuple<local_fd, remote_fd>: local_fd: local fd which should be sent
  // to sandboxee, remote_fd: it will be overwritten by local_fd.
  std::vector<std::tuple<int, int, std::string>> fd_map_;

  // Comms channel used to exchange data with the sandboxee.
  std::unique_ptr<Comms> comms_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_IPC_H_
