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

// sandbox2::ForkServer is a class which serves fork()ing request for the
// clients.

#ifndef SANDBOXED_API_SANDBOX2_FORKSERVER_H_
#define SANDBOXED_API_SANDBOX2_FORKSERVER_H_

#include <sys/types.h>

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/log/log.h"
#include "sandboxed_api/util/fileops.h"

namespace sandbox2 {

class Comms;
class ForkRequest;

class ForkServer {
 public:
  static constexpr int kSetupInitiated = 0x0C0A0B01;

  ForkServer(const ForkServer&) = delete;
  ForkServer& operator=(const ForkServer&) = delete;

  explicit ForkServer(Comms* comms) : comms_(comms) {
    if (!Initialize()) {
      LOG(FATAL) << "Could not initialize the ForkServer";
    }
  }

  // Returns whether the connection with the forkserver was terminated.
  bool IsTerminated() const;

  // Receives a fork request from the master process. The started process does
  // not need to be waited for (with waitid/waitpid/wait3/wait4) as the current
  // process will have the SIGCHLD set to sa_flags=SA_NOCLDWAIT.
  // Returns values defined as with fork() (-1 means error).
  pid_t ServeRequest();

 private:
  // Prepares the Fork-Server (worker side, not the requester side) for work by
  // sanitizing the environment:
  // - go down if the parent goes down,
  // - become subreaper - PR_SET_CHILD_SUBREAPER (man prctl),
  // - don't convert children processes into zombies if they terminate.
  bool Initialize();

  // Creates initial namespaces used as a template for namespaced sandboxees
  void CreateInitialNamespaces();
  void CreateInitialNamespacesImpl(Comms setup_comms);

  // Creates a network namespace to be shared between sandboxees
  void CreateForkserverSharedNetworkNamespace();
  void CreateEmptyNetworkNamespaceImpl(Comms setup_comms);

  // Comms channel which is used to send requests to this class. Not owned by
  // the object.
  Comms* comms_;
  uid_t orig_uid_ = -1;
  gid_t orig_gid_ = -1;
  sapi::file_util::fileops::FDCloser initial_mntns_fd_;
  sapi::file_util::fileops::FDCloser initial_userns_fd_;
  sapi::file_util::fileops::FDCloser shared_netns_fd_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_FORKSERVER_H_
