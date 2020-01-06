// Copyright 2020 Google LLC. All Rights Reserved.
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

// sandbox2::ForkServer is a class which serves fork()ing request for the
// clients.

#ifndef SANDBOXED_API_SANDBOX2_FORKSERVER_H_
#define SANDBOXED_API_SANDBOX2_FORKSERVER_H_

#include <sys/types.h>

#include <string>
#include <vector>

#include <glog/logging.h>
#include "absl/synchronization/mutex.h"

namespace sandbox2 {

class Comms;
class ForkRequest;

// Envvar indicating that this process should not start the fork-server.
static constexpr const char* kForkServerDisableEnv = "SANDBOX2_NOFORKSERVER";

class ForkClient {
 public:
  ForkClient(const ForkClient&) = delete;
  ForkClient& operator=(const ForkClient&) = delete;

  explicit ForkClient(Comms* comms) : comms_(comms) {}

  // Sends the fork request over the supplied Comms channel.
  pid_t SendRequest(const ForkRequest& request, int exec_fd, int comms_fd,
                    int user_ns_fd = -1, pid_t* init_pid = nullptr);

 private:
  // Comms channel connecting with the ForkServer. Not owned by the object.
  Comms* comms_;
  // Mutex locking transactions (requests) over the Comms channel.
  absl::Mutex comms_mutex_;
};

class ForkServer {
 public:
  ForkServer(const ForkServer&) = delete;
  ForkServer& operator=(const ForkServer&) = delete;

  explicit ForkServer(Comms* comms) : comms_(comms) {
    if (!Initialize()) {
      LOG(FATAL) << "Could not initialize the ForkServer";
    }
  }

  // Receives a fork request from the master process. The started process does
  // not need to be waited for (with waitid/waitpid/wait3/wait4) as the current
  // process will have the SIGCHLD set to sa_flags=SA_NOCLDWAIT.
  // Returns values defined as with fork() (-1 means error).
  pid_t ServeRequest();

 private:
  // Creates and launched the child process.
  void LaunchChild(const ForkRequest& request, int execve_fd, int client_fd,
                   uid_t uid, gid_t gid, int user_ns_fd, int signaling_fd,
                   bool avoid_pivot_root) const;

  // Prepares the Fork-Server (worker side, not the requester side) for work by
  // sanitizing the environment:
  // - go down if the parent goes down,
  // - become subreaper - PR_SET_CHILD_SUBREAPER (man prctl),
  // - don't convert children processes into zombies if they terminate.
  bool Initialize();

  // Creates initial namespaces used as a template for namespaced sandboxees
  void CreateInitialNamespaces();

  // Prepares arguments for the upcoming execve (if execve was requested).
  static void PrepareExecveArgs(const ForkRequest& request,
                                std::vector<std::string>* args,
                                std::vector<std::string>* envp);

  // Ensures that no unnecessary file descriptors are lingering after execve().
  static void SanitizeEnvironment(int client_fd);

  // Executes the sandboxee, or exit with Executor::kFailedExecve.
  static void ExecuteProcess(int execve_fd, const char** argv,
                             const char** envp);

  // Runs namespace initializers for a sandboxee.
  static void InitializeNamespaces(const ForkRequest& request, uid_t uid,
                                   gid_t gid, bool avoid_pivot_root);

  // Comms channel which is used to send requests to this class. Not owned by
  // the object.
  Comms* comms_;
  int initial_mntns_fd_ = -1;
  int initial_userns_fd_ = -1;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_FORKSERVER_H_
