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

#include "sandboxed_api/sandbox2/network_proxy/server.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/network_proxy/filtering.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/util/fileops.h"

namespace {

struct open_how {
  uint64_t flags;
  uint64_t mode;
  uint64_t resolve;
};

}  // anomymous namespace

#ifndef RESOLVE_NO_SYMLINKS
#define RESOLVE_NO_SYMLINKS 0x04
#endif

#ifndef __NR_openat2
#define __NR_openat2 437
#endif

namespace sandbox2 {

namespace file_util = ::sapi::file_util;

NetworkProxyServer::NetworkProxyServer(
    int fd, AllowedEndpoints* allowed_endpoints,
    absl::AnyInvocable<void()> notify_violation_fn)
    : violation_occurred_(false),
      comms_(std::make_unique<Comms>(fd)),
      fatal_error_(false),
      notify_violation_fn_(std::move(notify_violation_fn)),
      allowed_endpoints_(allowed_endpoints) {}

void NetworkProxyServer::ProcessConnectRequest() {
  std::vector<uint8_t> addr;
  if (!comms_->RecvBytes(&addr)) {
    fatal_error_ = true;
    return;
  }
  const struct sockaddr* saddr = reinterpret_cast<const sockaddr*>(addr.data());

  bool is_inet4 =
      (addr.size() == sizeof(sockaddr_in) && saddr->sa_family == AF_INET);
  bool is_inet6 =
      (addr.size() == sizeof(sockaddr_in6) && saddr->sa_family == AF_INET6);
  bool is_unix = (addr.size() > offsetof(struct sockaddr_un, sun_path) &&
                  saddr->sa_family == AF_UNIX);

  if (!is_inet4 && !is_inet6 && !is_unix) {
    SendError(EINVAL);
    return;
  }

  std::string canonical_path;
  if (!allowed_endpoints_->IsEndpointAllowed(saddr, addr.size(),
                                             &canonical_path)) {
    NotifyViolation(saddr, addr.size());
    return;
  }

  const struct sockaddr* connect_addr = saddr;
  socklen_t connect_len = addr.size();
  struct sockaddr_un proc_addr{};
  std::optional<file_util::fileops::FDCloser> o_path_closer;

  if (is_unix) {
    const struct sockaddr_un* sun_addr =
        reinterpret_cast<const struct sockaddr_un*>(addr.data());

    if (sun_addr->sun_path[0] != '\0') {
      struct open_how how = {};
      how.flags = O_PATH | O_CLOEXEC;
      how.resolve = RESOLVE_NO_SYMLINKS;

      int o_path_fd = static_cast<int>(
          util::Syscall(__NR_openat2, static_cast<uintptr_t>(AT_FDCWD),
                        reinterpret_cast<uintptr_t>(canonical_path.c_str()),
                        reinterpret_cast<uintptr_t>(&how), sizeof(how)));
      if (o_path_fd < 0) {
        SendError(errno);
        return;
      }
      o_path_closer.emplace(o_path_fd);

      struct stat st;
      if (fstat(o_path_fd, &st) < 0 || !S_ISSOCK(st.st_mode)) {
        SendError(ENOTSOCK);
        return;
      }

      proc_addr.sun_family = AF_UNIX;
      std::string proc_path = absl::StrCat("/proc/self/fd/", o_path_fd);
      strncpy(proc_addr.sun_path, proc_path.c_str(),
              sizeof(proc_addr.sun_path) - 1);

      connect_addr = reinterpret_cast<const struct sockaddr*>(&proc_addr);
      connect_len = sizeof(proc_addr);
    }
  }

  int new_socket = socket(saddr->sa_family, SOCK_STREAM, 0);
  if (new_socket < 0) {
    SendError(errno);
    return;
  }
  file_util::fileops::FDCloser new_socket_closer(new_socket);

  int result = connect(new_socket, connect_addr, connect_len);
  if (result < 0) {
    SendError(errno);
    return;
  }

  NotifySuccess();
  if (!fatal_error_ && !comms_->SendFD(new_socket)) {
    fatal_error_ = true;
  }
}

void NetworkProxyServer::Run() {
  while (!fatal_error_ &&
         !violation_occurred_.load(std::memory_order_relaxed)) {
    ProcessConnectRequest();
  }
  LOG(INFO)
      << "Clean shutdown or error occurred, shutting down NetworkProxyServer";
}

void NetworkProxyServer::SendError(int saved_errno) {
  if (!comms_->SendInt32(saved_errno)) {
    fatal_error_ = true;
  }
}

void NetworkProxyServer::NotifySuccess() {
  if (!comms_->SendInt32(0)) {
    fatal_error_ = true;
  }
}

void NetworkProxyServer::NotifyViolation(const struct sockaddr* saddr,
                                         socklen_t len) {
  if (absl::StatusOr<std::string> result = AddrToString(saddr, len);
      result.ok()) {
    violation_msg_ = std::move(result).value();
  } else {
    violation_msg_ = std::string(result.status().message());
  }
  violation_occurred_.store(true, std::memory_order_release);
  notify_violation_fn_();
}

}  // namespace sandbox2
