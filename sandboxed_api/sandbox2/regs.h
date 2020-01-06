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

// This file defines the sandbox2::Regs class stores context of a process
// during ptrace stop events

#ifndef SANDBOXED_API_SANDBOX2_REGS_H_
#define SANDBOXED_API_SANDBOX2_REGS_H_

#include <sys/types.h>

#include <cstdint>
#include <string>

#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/sandbox2/violation.pb.h"
#include "sandboxed_api/util/status.h"

namespace sandbox2 {

// Helper class to get and modify running processes registers. Uses ptrace and
// assumes the process is already attached.
class Regs {
 public:
#if !defined(__x86_64__) && !defined(__powerpc64__)
  static_assert(false, "No support for the current CPU architecture");
#endif

  explicit Regs(pid_t pid) : pid_(pid) {}

  // Copies register values from the process
  sapi::Status Fetch();

  // Copies register values to the process
  sapi::Status Store();

  // Causes the process to skip current syscall and return given value instead
  sapi::Status SkipSyscallReturnValue(uint64_t value);

  // Converts raw register values obtained on syscall entry to syscall info
  Syscall ToSyscall(Syscall::CpuArch syscall_arch) const;

  pid_t pid() const { return pid_; }

  // Stores register values in a protobuf structure.
  void StoreRegisterValuesInProtobuf(RegisterValues* values) const;

 private:
  friend class StackTracePeer;

  struct PtraceRegisters {
#if defined(__x86_64__)
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
#elif defined(__powerpc64__)
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
#endif
  };

  // PID for which registers are fetched/stored
  pid_t pid_ = 0;

  // Registers fetched with ptrace(PR_GETREGS/GETREGSET, pid).
  PtraceRegisters user_regs_ = {};
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_REGS_H_
