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

// Implementation file for the sandbox2::Client class.

#include "sandboxed_api/sandbox2/client.h"

#include <fcntl.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <syscall.h>
#include <unistd.h>

#include <cinttypes>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/sanitizer.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/strerror.h"

namespace sandbox2 {

using ::sapi::StrError;

Client::Client(Comms* comms) : comms_(comms) {
  char* fdmap_envvar = getenv(kFDMapEnvVar);
  if (!fdmap_envvar) {
    return;
  }
  absl::flat_hash_map<absl::string_view, absl::string_view> vars =
      absl::StrSplit(fdmap_envvar, ',', absl::SkipEmpty());
  for (const auto& [name, mapped_fd] : vars) {
    int fd;
    SAPI_RAW_CHECK(absl::SimpleAtoi(mapped_fd, &fd), "failed to parse fd map");
    SAPI_RAW_CHECK(fd_map_.emplace(std::string(name), fd).second,
                   "could not insert mapping into fd map (duplicate)");
  }
  unsetenv(kFDMapEnvVar);
}

std::string Client::GetFdMapEnvVar() const {
  return absl::StrCat(kFDMapEnvVar, "=",
                      absl::StrJoin(fd_map_, ",", absl::PairFormatter(",")));
}

void Client::PrepareEnvironment(std::vector<int>* preserve_fds) {
  SetUpIPC(preserve_fds);
  SetUpCwd();
}

void Client::EnableSandbox() {
  ReceivePolicy();
  ApplyPolicyAndBecomeTracee();
}

void Client::SandboxMeHere() {
  PrepareEnvironment();
  EnableSandbox();
}

void Client::SetUpCwd() {
  {
    // Get the current working directory to check if we are in a mount
    // namespace.
    // Note: glibc 2.27 no longer returns a relative path in that case, but
    //       fails with ENOENT and returns a nullptr instead. The code still
    //       needs to run on lower version for the time being.
    char cwd_buf[PATH_MAX + 1] = {0};
    char* cwd = getcwd(cwd_buf, ABSL_ARRAYSIZE(cwd_buf));
    SAPI_RAW_PCHECK(cwd != nullptr || errno == ENOENT,
                    "no current working directory");

    // Outside of the mount namespace, the path is of the form
    // '(unreachable)/...'. Only check for the slash, since Linux might make up
    // other prefixes in the future.
    if (errno == ENOENT || cwd_buf[0] != '/') {
      SAPI_RAW_VLOG(1, "chdir into mount namespace, cwd was '%s'", cwd_buf);
      // If we are in a mount namespace but fail to chdir, then it can lead to a
      // sandbox escape -- we need to fail with FATAL if the chdir fails.
      SAPI_RAW_PCHECK(chdir("/") != -1, "corrective chdir");
    }
  }

  // Receive the user-supplied current working directory and change into it.
  std::string cwd;
  SAPI_RAW_CHECK(comms_->RecvString(&cwd), "receiving working directory");
  if (!cwd.empty()) {
    // On the other hand this chdir can fail without a sandbox escape. It will
    // probably not have the intended behavior though.
    if (chdir(cwd.c_str()) == -1) {
      SAPI_RAW_VLOG(
          1,
          "chdir(%s) failed, falling back to previous cwd or / (with "
          "namespaces). Use Executor::SetCwd() to set a working directory: %s",
          cwd.c_str(), StrError(errno).c_str());
    }
  }
}

void Client::SetUpIPC(std::vector<int>* preserve_fds) {
  uint32_t num_of_fd_pairs;
  SAPI_RAW_CHECK(comms_->RecvUint32(&num_of_fd_pairs),
                 "receiving number of fd pairs");
  SAPI_RAW_CHECK(fd_map_.empty(), "fd map not empty");

  SAPI_RAW_VLOG(1, "Will receive %d file descriptor pairs", num_of_fd_pairs);

  absl::flat_hash_map<int, int*> preserve_fds_map;
  if (preserve_fds) {
    for (int& fd : *preserve_fds) {
      preserve_fds_map.emplace(fd, &fd);
    }
  }

  for (uint32_t i = 0; i < num_of_fd_pairs; ++i) {
    int32_t requested_fd;
    int32_t fd;
    std::string name;

    SAPI_RAW_CHECK(comms_->RecvInt32(&requested_fd), "receiving requested fd");
    SAPI_RAW_CHECK(comms_->RecvFD(&fd), "receiving current fd");
    SAPI_RAW_CHECK(comms_->RecvString(&name), "receiving name string");

    if (auto it = preserve_fds_map.find(requested_fd);
        it != preserve_fds_map.end()) {
      int old_fd = it->first;
      int new_fd = dup(old_fd);
      SAPI_RAW_PCHECK(new_fd != -1, "Failed to duplicate preserved fd=%d",
                      old_fd);
      SAPI_RAW_LOG(INFO, "Moved preserved fd=%d to %d", old_fd, new_fd);
      close(old_fd);
      int* pfd = it->second;
      *pfd = new_fd;
      preserve_fds_map.erase(it);
      preserve_fds_map.emplace(new_fd, pfd);
    }

    if (requested_fd == comms_->GetConnectionFD()) {
      comms_->MoveToAnotherFd();
      SAPI_RAW_LOG(INFO,
                   "Trying to map over comms fd (%d). Remapped comms to %d",
                   requested_fd, comms_->GetConnectionFD());
    }

    if (requested_fd != -1 && fd != requested_fd) {
      if (requested_fd > STDERR_FILENO && fcntl(requested_fd, F_GETFD) != -1) {
        // Dup2 will silently close the FD if one is already at requested_fd.
        // If someone is using the deferred sandbox entry, ie. SandboxMeHere,
        // the application might have something actually using that fd.
        // Therefore let's log a big warning if that FD is already in use.
        // Note: this check doesn't happen for STDIN,STDOUT,STDERR.
        SAPI_RAW_LOG(
            WARNING,
            "Cloning received fd %d over %d which is already open and will "
            "be silently closed. This may lead to unexpected behavior!",
            fd, requested_fd);
      }

      SAPI_RAW_VLOG(1, "Cloning received fd=%d onto fd=%d", fd, requested_fd);
      SAPI_RAW_PCHECK(dup2(fd, requested_fd) != -1, "");

      // Close the newly received FD if it differs from the new one.
      close(fd);
      fd = requested_fd;
    }

    if (!name.empty()) {
      SAPI_RAW_CHECK(fd_map_.emplace(name, fd).second, "duplicate fd mapping");
    }
  }
}

void Client::ReceivePolicy() {
  std::vector<uint8_t> bytes;
  SAPI_RAW_CHECK(comms_->RecvBytes(&bytes), "receive bytes");
  policy_ = std::move(bytes);
}

void Client::ApplyPolicyAndBecomeTracee() {
  // When running under *SAN, we need to notify *SANs background thread that we
  // want it to exit and wait for it to be done. When not running under *SAN,
  // this function does nothing.
  sanitizer::WaitForSanitizer();

  // Creds can be received w/o synchronization, once the connection is
  // established.
  pid_t cred_pid;
  uid_t cred_uid ABSL_ATTRIBUTE_UNUSED;
  gid_t cred_gid ABSL_ATTRIBUTE_UNUSED;
  SAPI_RAW_CHECK(comms_->RecvCreds(&cred_pid, &cred_uid, &cred_gid),
                 "receiving credentials");

  SAPI_RAW_CHECK(prctl(PR_SET_DUMPABLE, 1) == 0,
                 "setting PR_SET_DUMPABLE flag");
  if (prctl(PR_SET_PTRACER, cred_pid) == -1) {
    SAPI_RAW_VLOG(1, "No YAMA on this system. Continuing");
  }

  SAPI_RAW_CHECK(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0,
                 "setting PR_SET_NO_NEW_PRIVS flag");
  SAPI_RAW_CHECK(prctl(PR_SET_KEEPCAPS, 0) == 0,
                 "setting PR_SET_KEEPCAPS flag");

  sock_fprog prog;
  SAPI_RAW_CHECK(policy_.size() / sizeof(sock_filter) <=
                     std::numeric_limits<uint16_t>::max(),
                 "seccomp policy too long");
  prog.len = static_cast<uint16_t>(policy_.size() / sizeof(sock_filter));
  prog.filter = reinterpret_cast<sock_filter*>(&policy_.front());

  SAPI_RAW_VLOG(1,
                "Applying policy in PID %zd, sock_fprog.len: %" PRId16
                " entries (%" PRIuPTR " bytes)",
                syscall(__NR_gettid), prog.len, policy_.size());

  // Signal executor we are ready to have limits applied on us and be ptraced.
  // We want limits at the last moment to avoid triggering them too early and we
  // want ptrace at the last moment to avoid synchronization deadlocks.
  SAPI_RAW_CHECK(comms_->SendUint32(kClient2SandboxReady),
                 "receiving ready signal from executor");
  uint32_t ret;  // wait for confirmation
  SAPI_RAW_CHECK(comms_->RecvUint32(&ret),
                 "receving confirmation from executor");
  SAPI_RAW_CHECK(ret == kSandbox2ClientDone,
                 "invalid confirmation from executor");

  int result =
      syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC,
              reinterpret_cast<uintptr_t>(&prog));
  SAPI_RAW_PCHECK(result != -1, "setting seccomp filter");
  SAPI_RAW_PCHECK(result == 0,
                  "synchronizing threads using SECCOMP_FILTER_FLAG_TSYNC flag "
                  "for thread=%d",
                  result);
}

int Client::GetMappedFD(const std::string& name) {
  auto it = fd_map_.find(name);
  SAPI_RAW_CHECK(it != fd_map_.end(),
                 "mapped fd not found (function called twice?)");
  int fd = it->second;
  fd_map_.erase(it);
  return fd;
}

bool Client::HasMappedFD(const std::string& name) {
  return fd_map_.find(name) != fd_map_.end();
}

void Client::SendLogsToSupervisor() {
  // This LogSink will register itself and send all logs to the executor until
  // the object is destroyed.
  logsink_ = std::make_unique<LogSink>(GetMappedFD(LogSink::kLogFDName));
}

NetworkProxyClient* Client::GetNetworkProxyClient() {
  if (proxy_client_ == nullptr) {
    proxy_client_ = std::make_unique<NetworkProxyClient>(
        GetMappedFD(NetworkProxyClient::kFDName));
  }
  return proxy_client_.get();
}

absl::Status Client::InstallNetworkProxyHandler() {
  if (fd_map_.find(NetworkProxyClient::kFDName) == fd_map_.end()) {
    return absl::FailedPreconditionError(
        "InstallNetworkProxyHandler() must be called at most once after the "
        "sandbox is installed. Also, the NetworkProxyServer needs to be "
        "enabled.");
  }
  return NetworkProxyHandler::InstallNetworkProxyHandler(
      GetNetworkProxyClient());
}

}  // namespace sandbox2
