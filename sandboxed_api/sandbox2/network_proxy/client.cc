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

#include "sandboxed_api/sandbox2/network_proxy/client.h"

#include <linux/net.h>
#include <linux/seccomp.h>
#include <stdio.h>
#include <syscall.h>
#include <ucontext.h>

#include <cerrno>
#include <iostream>

#include <glog/logging.h>
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/util/status_macros.h"
#include "sandboxed_api/util/strerror.h"

namespace sandbox2 {

using ::sapi::StrError;

#ifndef SYS_SECCOMP
constexpr int SYS_SECCOMP = 1;
#endif

#if defined(SAPI_X86_64)
constexpr int kRegResult = REG_RAX;
constexpr int kRegSyscall = REG_RAX;
constexpr int kRegArg0 = REG_RDI;
constexpr int kRegArg1 = REG_RSI;
constexpr int kRegArg2 = REG_RDX;
#elif defined(SAPI_PPC64_LE)
constexpr int kRegResult = 3;
constexpr int kRegSyscall = 0;
constexpr int kRegArg0 = 3;
constexpr int kRegArg1 = 4;
constexpr int kRegArg2 = 5;
#elif defined(SAPI_ARM64)
constexpr int kRegResult = 0;
constexpr int kRegSyscall = 8;
constexpr int kRegArg0 = 0;
constexpr int kRegArg1 = 1;
constexpr int kRegArg2 = 2;
#elif defined(SAPI_ARM)
constexpr int kRegResult = 0;
constexpr int kRegSyscall = 8;
constexpr int kRegArg0 = 0;
constexpr int kRegArg1 = 1;
constexpr int kRegArg2 = 2;
#endif

int NetworkProxyClient::ConnectHandler(int sockfd, const struct sockaddr* addr,
                                       socklen_t addrlen) {
  absl::Status status = Connect(sockfd, addr, addrlen);
  if (status.ok()) {
    return 0;
  }
  LOG(ERROR) << "ConnectHandler() failed: " << status.message();
  return -1;
}

absl::Status NetworkProxyClient::Connect(int sockfd,
                                         const struct sockaddr* addr,
                                         socklen_t addrlen) {
  absl::MutexLock lock(&mutex_);

  // Check if socket is SOCK_STREAM
  int type;
  socklen_t type_size = sizeof(int);
  int result = getsockopt(sockfd, SOL_SOCKET, SO_TYPE, &type, &type_size);
  if (result == -1) {
    return absl::FailedPreconditionError("Invalid socket FD");
  }
  if (type_size != sizeof(int) || type != SOCK_STREAM) {
    errno = EINVAL;
    return absl::InvalidArgumentError(
        "Invalid socket, only SOCK_STREAM is allowed");
  }

  // Send sockaddr struct
  if (!comms_.SendBytes(reinterpret_cast<const uint8_t*>(addr), addrlen)) {
    errno = EIO;
    return absl::InternalError("Sending data to network proxy failed");
  }

  SAPI_RETURN_IF_ERROR(ReceiveRemoteResult());

  // Receive new socket
  int s;
  if (!comms_.RecvFD(&s)) {
    errno = EIO;
    return absl::InternalError("Receiving data from network proxy failed");
  }
  if (dup2(s, sockfd) == -1) {
    close(s);
    return absl::InternalError("Processing data from network proxy failed");
  }
  return absl::OkStatus();
}

absl::Status NetworkProxyClient::ReceiveRemoteResult() {
  int result;
  if (!comms_.RecvInt32(&result)) {
    errno = EIO;
    return absl::InternalError("Receiving data from the network proxy failed");
  }
  if (result != 0) {
    errno = result;
    return absl::InternalError(
        absl::StrCat("Error in network proxy server: ", StrError(errno)));
  }
  return absl::OkStatus();
}

namespace {

static NetworkProxyHandler* g_network_proxy_handler = nullptr;

void SignalHandler(int nr, siginfo_t* info, void* void_context) {
  g_network_proxy_handler->ProcessSeccompTrap(nr, info, void_context);
}

}  // namespace

absl::Status NetworkProxyHandler::InstallNetworkProxyHandler(
    NetworkProxyClient* npc) {
  if (g_network_proxy_handler) {
    return absl::AlreadyExistsError(
        "Network proxy handler is already installed");
  }
  g_network_proxy_handler = new NetworkProxyHandler(npc);
  return absl::OkStatus();
}

void NetworkProxyHandler::InvokeOldAct(int nr, siginfo_t* info,
                                       void* void_context) {
  if (oldact_.sa_flags & SA_SIGINFO) {
    if (oldact_.sa_sigaction) {
      oldact_.sa_sigaction(nr, info, void_context);
    }
  } else if (oldact_.sa_handler == SIG_IGN) {
    return;
  } else if (oldact_.sa_handler == SIG_DFL) {
    sigaction(SIGSYS, &oldact_, nullptr);
    raise(SIGSYS);
  } else if (oldact_.sa_handler) {
    oldact_.sa_handler(nr);
  }
}  // namespace sandbox2

void NetworkProxyHandler::ProcessSeccompTrap(int nr, siginfo_t* info,
                                             void* void_context) {
  if (info->si_code != SYS_SECCOMP) {
    InvokeOldAct(nr, info, void_context);
    return;
  }
  auto* ctx = static_cast<ucontext_t*>(void_context);
  if (!ctx) {
    return;
  }

#if defined(SAPI_X86_64)
  auto* registers = ctx->uc_mcontext.gregs;
#elif defined(SAPI_PPC64_LE)
  auto* registers = ctx->uc_mcontext.gp_regs;
#elif defined(SAPI_ARM64)
  auto* registers = ctx->uc_mcontext.regs;
#elif defined(SAPI_ARM)
  auto* registers = &ctx->uc_mcontext.arm_r0;
#endif
  int syscall = registers[kRegSyscall];

  int sockfd;
  const struct sockaddr* addr;
  socklen_t addrlen;

  if (syscall == __NR_connect) {
    sockfd = static_cast<int>(registers[kRegArg0]);
    addr = reinterpret_cast<const struct sockaddr*>(registers[kRegArg1]);
    addrlen = static_cast<socklen_t>(registers[kRegArg2]);
#if defined(SAPI_PPC64_LE)
  } else if (syscall == __NR_socketcall &&
             static_cast<int>(registers[kRegArg0]) == SYS_CONNECT) {
    auto* connect_args = reinterpret_cast<uint64_t*>(registers[kRegArg1]);
    sockfd = static_cast<int>(connect_args[0]);
    addr = reinterpret_cast<const struct sockaddr*>(connect_args[1]);
    addrlen = static_cast<socklen_t>(connect_args[2]);
#endif
  } else {
    InvokeOldAct(nr, info, void_context);
    return;
  }

  absl::Status result = network_proxy_client_->Connect(sockfd, addr, addrlen);
  if (result.ok()) {
    registers[kRegResult] = 0;
  } else {
    registers[kRegResult] = -errno;
  }
}

void NetworkProxyHandler::InstallSeccompTrap() {
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGSYS);

  struct sigaction act = {};
  act.sa_sigaction = &SignalHandler;
  act.sa_flags = SA_SIGINFO;

  CHECK_EQ(sigaction(SIGSYS, &act, &oldact_), 0);
  CHECK_EQ(sigprocmask(SIG_UNBLOCK, &mask, nullptr), 0);
}

}  // namespace sandbox2
