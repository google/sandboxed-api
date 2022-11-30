// Copyright 2022 Google LLC
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

#include "sandboxed_api/sandbox2/util/syscall_trap.h"

#include <ucontext.h>

#include "absl/log/check.h"
#include "sandboxed_api/config.h"

namespace sandbox2 {
namespace {

#if defined(SAPI_X86_64)
constexpr int kRegResult = REG_RAX;
constexpr int kRegSyscall = REG_RAX;
constexpr std::array kRegArgs = {REG_RDI, REG_RSI, REG_RDX,
                                 REG_R10, REG_R8,  REG_R9};
#elif defined(SAPI_PPC64_LE)
constexpr int kRegResult = 3;
constexpr int kRegSyscall = 0;
constexpr std::array kRegArgs = {3, 4, 5, 6, 7, 8};
#elif defined(SAPI_ARM64)
constexpr int kRegResult = 0;
constexpr int kRegSyscall = 8;
constexpr std::array kRegArgs = {0, 1, 2, 3, 4, 5};
#elif defined(SAPI_ARM)
constexpr int kRegResult = 0;
constexpr int kRegSyscall = 8;
constexpr std::array kRegArgs = {0, 1, 2, 3, 4, 5};
#endif

#ifndef SYS_SECCOMP
constexpr int SYS_SECCOMP = 1;
#endif

SyscallTrap* g_instance = nullptr;

}  // namespace

void SyscallTrap::SignalHandler(int nr, siginfo_t* info, void* context) {
  return g_instance->SignalHandlerImpl(nr, info, context);
}

void SyscallTrap::InvokeOldAct(int nr, siginfo_t* info, void* context) {
  if (oldact_.sa_flags & SA_SIGINFO) {
    if (oldact_.sa_sigaction) {
      oldact_.sa_sigaction(nr, info, context);
    }
  } else if (oldact_.sa_handler == SIG_IGN) {
    return;
  } else if (oldact_.sa_handler == SIG_DFL) {
    sigaction(SIGSYS, &oldact_, nullptr);
    raise(SIGSYS);
  } else if (oldact_.sa_handler) {
    oldact_.sa_handler(nr);
  }
}

void SyscallTrap::SignalHandlerImpl(int nr, siginfo_t* info, void* context) {
  int old_errno = errno;
  if (nr != SIGSYS) {
    InvokeOldAct(nr, info, context);
    errno = old_errno;
    return;
  }
  if (info->si_code != SYS_SECCOMP) {
    InvokeOldAct(nr, info, context);
    errno = old_errno;
    return;
  }
  auto* uctx = static_cast<ucontext_t*>(context);
  if (!uctx) {
    errno = old_errno;
    return;
  }

#if defined(SAPI_X86_64)
  auto* registers = uctx->uc_mcontext.gregs;
#elif defined(SAPI_PPC64_LE)
  auto* registers = uctx->uc_mcontext.gp_regs;
#elif defined(SAPI_ARM64)
  auto* registers = uctx->uc_mcontext.regs;
#elif defined(SAPI_ARM)
  auto* registers = &uctx->uc_mcontext.arm_r0;
#endif
  int syscall_nr = registers[kRegSyscall];
  Args args = {registers[kRegArgs[0]], registers[kRegArgs[1]],
               registers[kRegArgs[2]], registers[kRegArgs[3]],
               registers[kRegArgs[4]], registers[kRegArgs[5]]};
  uintptr_t rv;
  if (!handler_(syscall_nr, args, &rv)) {
    InvokeOldAct(nr, info, context);
    errno = old_errno;
    return;
  }
  registers[kRegResult] = rv;
}

bool SyscallTrap::Install(bool (*handler)(int nr, Args args,
                                          uintptr_t* result)) {
  if (g_instance) {
    return false;
  }
  g_instance = new SyscallTrap(handler);
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGSYS);

  struct sigaction act = {};
  act.sa_sigaction = &SignalHandler;
  act.sa_flags = SA_SIGINFO;

  CHECK_EQ(sigaction(SIGSYS, &act, &g_instance->oldact_), 0);
  CHECK_EQ(sigprocmask(SIG_UNBLOCK, &mask, nullptr), 0);
  return true;
}

}  // namespace sandbox2
