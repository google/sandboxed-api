// Copyright 2019 Google LLC
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

// Implementation of the sandbox2::ForkServer class.

#include "sandboxed_api/sandbox2/global_forkclient.h"

#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <syscall.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>

#include "absl/base/attributes.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/embed_file.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/forkserver.h"
#include "sandboxed_api/sandbox2/forkserver_bin_embed.h"
#include "sandboxed_api/sandbox2/sanitizer.h"
#include "sandboxed_api/sandbox2/util/strerror.h"
#include "sandboxed_api/util/raw_logging.h"

namespace sandbox2 {

// Global ForkClient object linking with the global ForkServer.
static ForkClient* global_fork_client = nullptr;
static pid_t global_fork_server_pid = -1;

ForkClient* GetGlobalForkClient() {
  SAPI_RAW_CHECK(global_fork_client != nullptr,
                 "global fork client not initialized");
  return global_fork_client;
}

pid_t GetGlobalForkServerPid() { return global_fork_server_pid; }

static void StartGlobalForkServer() {
  SAPI_RAW_CHECK(global_fork_client == nullptr,
                 "global fork server already initialized");
  if (getenv(kForkServerDisableEnv)) {
    SAPI_RAW_VLOG(1,
                  "Start of the Global Fork-Server prevented by the '%s' "
                  "environment variable present",
                  kForkServerDisableEnv);
    return;
  }

  sanitizer::WaitForTsan();

  // We should be really single-threaded now, as it's the point of the whole
  // exercise.
  int num_threads = sanitizer::GetNumberOfThreads(getpid());
  if (num_threads != 1) {
    SAPI_RAW_LOG(ERROR,
                 "BADNESS MAY HAPPEN. ForkServer::Init() created in a "
                 "multi-threaded context, %d threads present",
                 num_threads);
  }

  int sv[2];
  SAPI_RAW_CHECK(socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) != -1,
                 "creating socket pair");

  // Fork the fork-server, and clean-up the resources (close remote sockets).
  pid_t pid;
  {
    pid = fork();
  }
  SAPI_RAW_PCHECK(pid != -1, "during fork");

  // Parent.
  if (pid > 0) {
    close(sv[0]);
    global_fork_client = new ForkClient{new Comms{sv[1]}};
    global_fork_server_pid = pid;
    return;
  }

  // Move the comms FD to the proper, expected FD number.
  // The new FD will not be CLOEXEC, which is what we want.
  dup2(sv[0], Comms::kSandbox2ClientCommsFD);

  int exec_fd = sapi::EmbedFile::GetEmbedFileSingleton()->GetFdForFileToc(
      forkserver_bin_embed_create());
  SAPI_RAW_CHECK(exec_fd >= 0, "Getting FD for init binary failed");

  char* const args[] = {strdup("S2-FORK-SERV"), nullptr};
  char* const envp[] = {nullptr};
  syscall(__NR_execveat, exec_fd, "", args, envp, AT_EMPTY_PATH);
  SAPI_RAW_PCHECK(false, "Could not launch forkserver binary");
}

}  // namespace sandbox2

// Run the ForkServer from the constructor, when no other threads are present.
// Because it's possible to start thread-inducing initializers before
// RunInitializers() (base/googleinit.h) it's not enough to just register
// a 0000_<name> initializer instead.
ABSL_ATTRIBUTE_UNUSED
__attribute__((constructor)) static void StartSandbox2Forkserver() {
  sandbox2::StartGlobalForkServer();
}
