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

#include "sandboxed_api/sandbox2/unwind/ptrace_hook.h"

#include <sys/ptrace.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

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

// Whether ptrace() emulation is in effect. This can only be enabled (per
// thread), never disabled.
thread_local bool g_emulate_ptrace = false;

}  // namespace

void EnablePtraceEmulationWithUserRegs(absl::string_view regs) {
  g_registers->resize((regs.size() + 1) / kRegSize);
  memcpy(&g_registers->front(), regs.data(), regs.size());
  g_emulate_ptrace = true;
}

// Replaces the libc version of ptrace.
// This wrapper makes use of process_vm_readv to read process memory instead of
// issuing ptrace syscalls. Accesses to registers will be emulated, for this the
// register values should be set via EnablePtraceEmulationWithUserRegs().
extern "C" long int ptrace_wrapped(  // NOLINT
    PtraceRequest request, pid_t pid, void* addr, void* data) {
  if (!g_emulate_ptrace) {
    return ptrace(request, pid, addr, data);
  }

  switch (request) {
    case PTRACE_PEEKDATA: {
      long int read_data;  // NOLINT
      struct iovec local = {
          .iov_base = &read_data,
          .iov_len = sizeof(long int),  // NOLINT
      };
      struct iovec remote = {
          .iov_base = addr,
          .iov_len = sizeof(long int),  // NOLINT
      };

      if (process_vm_readv(pid, &local, 1, &remote, 1, 0) <= 0) {
        return -1;
      }
      return read_data;
    }
    case PTRACE_PEEKUSER: {
      // Make sure read is in-bounds and aligned.
      auto offset = reinterpret_cast<uintptr_t>(addr);
      if (offset + kRegSize > g_registers->size() * kRegSize ||
          offset % kRegSize != 0) {
        return -1;
      }
      return (*g_registers)[offset / kRegSize];
    }
    default:
      fprintf(stderr, "ptrace_wrapped(): operation not permitted: %d\n",
              request);
      _exit(1);
  }
  return 0;
}

}  // namespace sandbox2
