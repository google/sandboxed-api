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

#ifndef SANDBOXED_API_SANDBOX2_FORKINGCLIENT_H_
#define SANDBOXED_API_SANDBOX2_FORKINGCLIENT_H_

#include <sys/types.h>
#include <memory>

#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/forkserver.h"

namespace sandbox2 {

class ForkingClient : public Client {
 public:
  explicit ForkingClient(Comms* comms) : Client(comms) {}

  // Forks the current process (if asked by the Executor in the parent process),
  // and returns the newly created PID to this Executor. This is used if the
  // current Client objects acts as a wrapper of ForkServer (and this process
  // was created to act as a ForkServer).
  // Return values specified as with 'fork' (incl. -1).
  pid_t WaitAndFork();

 private:
  // ForkServer object, which is used only if the current process is meant
  // to behave like a Fork-Server, i.e. to create a new process which will be
  // later sandboxed (with SandboxMeHere()).
  std::unique_ptr<ForkServer> fork_server_worker_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_FORKINGCLIENT_H_
