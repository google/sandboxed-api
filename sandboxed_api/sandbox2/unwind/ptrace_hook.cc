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

#include "sandboxed_api/sandbox2/unwind/ptrace_hook.h"

#include <sys/ptrace.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

// Maximum register struct size: 128 u64.
constexpr size_t kRegisterBufferSize = 128 * 8;
// Contains the register values in a ptrace specified format.
// This format is pretty opaque which is why we just forward
// the raw bytes (up to a certain limit).
static unsigned char register_values[kRegisterBufferSize];
static size_t n_register_values_bytes_used = 0;

// It should not be necessary to put this in a thread local storage as we
// do not support setting up the forkserver when there is more than one thread.
// However there might be some edge-cases, so we do this just in case.
thread_local bool emulate_ptrace = false;

void ArmPtraceEmulation() { emulate_ptrace = true; }

void InstallUserRegs(const char *ptr, size_t size) {
  if (sizeof(register_values) < size) {
    fprintf(stderr, "install_user_regs: Got more bytes than supported (%lu)\n",
            size);
  } else {
    memcpy(&register_values, ptr, size);
    n_register_values_bytes_used = size;
  }
}

// Replaces the libc version of ptrace.
// This wrapper makes use of process_vm_readv to read process memory instead of
// issuing ptrace syscalls. Accesses to registers will be emulated, for this the
// register values should be set via install_user_regs().
// The emulation can be switched on using arm_ptrace_emulation().
extern "C" long int ptrace_wrapped(  // NOLINT
    enum __ptrace_request request, pid_t pid, void *addr, void *data) {
  // Register size is `long` for the supported architectures according to the
  // kernel.
  using reg_type = long;  // NOLINT
  constexpr size_t reg_size = sizeof(reg_type);
  if (!emulate_ptrace) {
    return ptrace(request, pid, addr, data);
  }
  switch (request) {
    case PTRACE_PEEKDATA: {
      long int read_data;  // NOLINT

      struct iovec local, remote;
      local.iov_len = sizeof(long int);  // NOLINT
      local.iov_base = &read_data;

      remote.iov_len = sizeof(long int);  // NOLINT
      remote.iov_base = addr;

      if (process_vm_readv(pid, &local, 1, &remote, 1, 0) > 0) {
        return read_data;
      } else {
        return -1;
      }
    } break;
    case PTRACE_PEEKUSER: {
      uintptr_t next_offset = reinterpret_cast<uintptr_t>(addr) + reg_size;
      // Make sure read is in-bounds and aligned.
      if (next_offset <= n_register_values_bytes_used &&
          reinterpret_cast<uintptr_t>(addr) % reg_size == 0) {
        return reinterpret_cast<reg_type *>(
            &register_values)[reinterpret_cast<uintptr_t>(addr) / reg_size];
      } else {
        return -1;
      }
    } break;
    default: {
      fprintf(stderr, "ptrace-wrapper: forbidden operation invoked: %d\n",
              request);
      _exit(1);
    }
  }

  return 0;
}
