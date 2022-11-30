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

#include "sandboxed_api/sandbox2/unwind/ptrace_hook.h"

#include <elf.h>  // For NT_PRSTATUS
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <syscall.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "sandboxed_api/sandbox2/util/syscall_trap.h"

// Android doesn't use an enum for __ptrace_request, use int instead.
#if defined(__ANDROID__)
using PtraceRequest = int;
#else
using PtraceRequest = __ptrace_request;
#endif

namespace sandbox2 {
namespace {

// Register size is `long` for the supported architectures according to the
// kernel.
using RegType = long;  // NOLINT
constexpr size_t kRegSize = sizeof(RegType);

// Contains the register values in a ptrace specified format. This format is
// pretty opaque which is why we just forward the raw bytes (up to a certain
// limit).
auto* g_registers = new std::vector<RegType>();

// Hooks ptrace.
// This wrapper makes use of process_vm_readv to read process memory instead of
// issuing ptrace syscalls. Accesses to registers will be emulated, for this the
// register values should be set via EnablePtraceEmulationWithUserRegs().
long int ptrace_hook(  // NOLINT
    PtraceRequest request, pid_t pid, void* addr, void* data) {
  switch (request) {
    case PTRACE_PEEKDATA: {
      RegType read_data;
      iovec local = {.iov_base = &read_data, .iov_len = sizeof(read_data)};
      iovec remote = {.iov_base = addr, .iov_len = sizeof(read_data)};

      if (process_vm_readv(pid, &local, 1, &remote, 1, 0) <= 0) {
        return -1;
      }
      *reinterpret_cast<RegType*>(data) = read_data;
      break;
    }
    case PTRACE_PEEKUSER: {
      // Make sure read is in-bounds and aligned.
      auto offset = reinterpret_cast<uintptr_t>(addr);
      if (offset + kRegSize > g_registers->size() * kRegSize ||
          offset % kRegSize != 0) {
        return -1;
      }
      *reinterpret_cast<RegType*>(data) = (*g_registers)[offset / kRegSize];
      break;
    }
    case PTRACE_GETREGSET: {
      // Only return general-purpose registers.
      if (auto kind = reinterpret_cast<uintptr_t>(addr); kind != NT_PRSTATUS) {
        return -1;
      }
      auto reg_set = reinterpret_cast<iovec*>(data);
      if (reg_set->iov_len > g_registers->size() * kRegSize) {
        return -1;
      }
      memcpy(reg_set->iov_base, g_registers->data(), reg_set->iov_len);
      break;
    }
    default:
      fprintf(stderr, "ptrace_hook(): operation not permitted: %d\n", request);
      abort();
  }
  return 0;
}

}  // namespace

void EnablePtraceEmulationWithUserRegs(absl::string_view regs) {
  g_registers->resize((regs.size() + 1) / kRegSize);
  memcpy(&g_registers->front(), regs.data(), regs.size());
  SyscallTrap::Install([](int nr, SyscallTrap::Args args, uintptr_t* rv) {
    if (nr != __NR_ptrace) {
      return false;
    }
    *rv = ptrace_hook(
        static_cast<PtraceRequest>(args[0]), static_cast<pid_t>(args[1]),
        reinterpret_cast<void*>(args[2]), reinterpret_cast<void*>(args[3]));
    return true;
  });
}

}  // namespace sandbox2
