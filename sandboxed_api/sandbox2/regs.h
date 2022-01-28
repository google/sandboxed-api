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

// This file defines the sandbox2::Regs class stores context of a process
// during ptrace stop events

#ifndef SANDBOXED_API_SANDBOX2_REGS_H_
#define SANDBOXED_API_SANDBOX2_REGS_H_

#include <sys/types.h>

#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/sandbox2/violation.pb.h"

namespace sandbox2 {

// Helper class to get and modify running processes registers. Uses ptrace and
// assumes the process is already attached.
class Regs {
 public:
  explicit Regs(pid_t pid) : pid_(pid) {}

  // Copies register values from the process
  absl::Status Fetch();

  // Copies register values to the process
  absl::Status Store();

  // Causes the process to skip current syscall and return given value instead
  absl::Status SkipSyscallReturnValue(uintptr_t value);

  // Converts raw register values obtained on syscall entry to syscall info
  Syscall ToSyscall(sapi::cpu::Architecture syscall_arch) const;

  // Returns the content of the register that holds a syscall's return value
  int64_t GetReturnValue(sapi::cpu::Architecture syscall_arch) const;

  pid_t pid() const { return pid_; }

  // Stores register values in a protobuf structure.
  void StoreRegisterValuesInProtobuf(RegisterValues* values) const;

 private:
  friend class StackTracePeer;

  struct PtraceRegisters {
#if defined(SAPI_X86_64)
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t orig_rax;
    uint64_t rip;
    uint64_t cs;
    uint64_t eflags;
    uint64_t rsp;
    uint64_t ss;
    uint64_t fs_base;
    uint64_t gs_base;
    uint64_t ds;
    uint64_t es;
    uint64_t fs;
    uint64_t gs;
#elif defined(SAPI_PPC64_LE)
    uint64_t gpr[32];
    uint64_t nip;
    uint64_t msr;
    uint64_t orig_gpr3;
    uint64_t ctr;
    uint64_t link;
    uint64_t xer;
    uint64_t ccr;
    uint64_t softe;
    uint64_t trap;
    uint64_t dar;
    uint64_t dsisr;
    uint64_t result;
    // elf.h's ELF_NGREG says it's 48 registers, so kernel fills it in with some
    // zeroes.
    uint64_t zero0;
    uint64_t zero1;
    uint64_t zero2;
    uint64_t zero3;
#elif defined(SAPI_ARM64)
    uint64_t regs[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
#elif defined(SAPI_ARM)
    uint32_t regs[15];
    uint32_t pc;
    uint32_t cpsr;
    uint32_t orig_x0;
#else
    static_assert(false, "Host CPU architecture not supported, see config.h");
#endif
  };

  // PID for which registers are fetched/stored
  pid_t pid_ = 0;

  // Registers fetched with ptrace(PR_GETREGS/GETREGSET, pid).
  PtraceRegisters user_regs_ = {};

  // On AArch64, obtaining the syscall number needs a specific call to ptrace()
  int syscall_number_ = 0;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_REGS_H_
