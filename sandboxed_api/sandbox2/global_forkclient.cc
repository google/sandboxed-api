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

#include <glog/logging.h>
#include "sandboxed_api/util/flag.h"
#include "absl/memory/memory.h"
#include "sandboxed_api/embed_file.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/forkserver_bin_embed.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/sandbox2/util/strerror.h"
#include "sandboxed_api/util/raw_logging.h"

ABSL_FLAG(bool, sandbox2_start_forkserver, true,
          "Start Sandbox2 Forkserver process");

namespace sandbox2 {

namespace {

std::unique_ptr<GlobalForkClient> StartGlobalForkServer() {
  if (getenv(kForkServerDisableEnv)) {
    SAPI_RAW_VLOG(1,
                  "Start of the Global Fork-Server prevented by the '%s' "
                  "environment variable present",
                  kForkServerDisableEnv);
    return {};
  }

  if (!absl::GetFlag(FLAGS_sandbox2_start_forkserver)) {
    SAPI_RAW_VLOG(
        1, "Start of the Global Fork-Server prevented by commandline flag");
    return {};
  }

  file_util::fileops::FDCloser exec_fd(
      sapi::EmbedFile::GetEmbedFileSingleton()->GetFdForFileToc(
          forkserver_bin_embed_create()));
  SAPI_RAW_CHECK(exec_fd.get() >= 0, "Getting FD for init binary failed");

  std::string proc_name = "S2-FORK-SERV";

  int sv[2];
  SAPI_RAW_CHECK(socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) != -1,
                 "creating socket pair");

  // Fork the fork-server, and clean-up the resources (close remote sockets).
  pid_t pid = util::ForkWithFlags(SIGCHLD);
  SAPI_RAW_PCHECK(pid != -1, "during fork");

  // Child.
  if (pid == 0) {
    // Move the comms FD to the proper, expected FD number.
    // The new FD will not be CLOEXEC, which is what we want.
    dup2(sv[0], Comms::kSandbox2ClientCommsFD);

    char* const args[] = {proc_name.data(), nullptr};
    char* const envp[] = {nullptr};
    syscall(__NR_execveat, exec_fd.get(), "", args, envp, AT_EMPTY_PATH);
    SAPI_RAW_PLOG(FATAL, "Could not launch forkserver binary");
    abort();
  }

  close(sv[0]);
  return absl::make_unique<GlobalForkClient>(sv[1], pid);
}

GlobalForkClient* GetGlobalForkClient() {
  static GlobalForkClient* global_fork_client =
      StartGlobalForkServer().release();
  return global_fork_client;
}

}  // namespace

void GlobalForkClient::EnsureStarted() { GetGlobalForkClient(); }

pid_t GlobalForkClient::SendRequest(const ForkRequest& request, int exec_fd,
                                    int comms_fd, int user_ns_fd,
                                    pid_t* init_pid) {
  GlobalForkClient* global_fork_client = GetGlobalForkClient();
  SAPI_RAW_CHECK(global_fork_client != nullptr,
                 "global fork client not initialized");
  pid_t pid = global_fork_client->fork_client_.SendRequest(
      request, exec_fd, comms_fd, user_ns_fd, init_pid);
  if (global_fork_client->comms_.IsTerminated()) {
    LOG(ERROR) << "Global forkserver connection terminated";
  }
  return pid;
}

pid_t GlobalForkClient::GetPid() {
  GlobalForkClient* global_fork_client = GetGlobalForkClient();
  SAPI_RAW_CHECK(global_fork_client != nullptr,
                 "global fork client not initialized");
  return global_fork_client->fork_client_.pid();
}
}  // namespace sandbox2
