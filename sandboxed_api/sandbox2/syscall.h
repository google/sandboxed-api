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

// The sandbox2::Syscalls class defines mostly static helper methods which
// are used to analyze status of the ptraced process

#ifndef SANDBOXED_API_SANDBOX2_SYSCALL_H__
#define SANDBOXED_API_SANDBOX2_SYSCALL_H__

#include <sys/types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sandbox2 {

class Syscall {
 public:
  // Supported CPU architectures.
  // Linux: Use a magic value, so it can be easily spotted in the seccomp-bpf
  // bytecode decompilation stream. Must be < (1<<15), as/ that's the size of
  // data which can be returned by BPF.
  enum CpuArch {
    kUnknown = 0xCAF0,
    kX86_64,
    kX86_32,
    kPPC_64,
  };
  // Maximum number of syscall arguments
  static constexpr size_t kMaxArgs = 6;
  using Args = std::array<uint64_t, kMaxArgs>;

  // Returns the host architecture, according to CpuArch.
  static CpuArch GetHostArch();

  // Returns the host architecture, according to <linux/audit.h>.
  static uint32_t GetHostAuditArch();

  // Returns a description of the architecture.
  static std::string GetArchDescription(CpuArch arch);

  Syscall() = default;
  Syscall(CpuArch arch, uint64_t nr, Args args = {})
      : arch_(arch), nr_(nr), args_(args) {}

  pid_t pid() const { return pid_; }
  uint64_t nr() const { return nr_; }
  CpuArch arch() const { return arch_; }
  const Args& args() const { return args_; }
  uint64_t stack_pointer() const { return sp_; }
  uint64_t instruction_pointer() const { return ip_; }

  std::string GetName() const;

  std::vector<std::string> GetArgumentsDescription() const;
  std::string GetDescription() const;

 private:
  friend class Regs;

  Syscall(pid_t pid) : pid_(pid) {}
  Syscall(CpuArch arch, uint64_t nr, Args args, pid_t pid, uint64_t sp,
          uint64_t ip)
      : arch_(arch), nr_(nr), args_(args), pid_(pid), sp_(sp), ip_(ip) {}

  CpuArch arch_ = kUnknown;
  uint64_t nr_ = -1;
  Args args_ = {};
  pid_t pid_ = -1;
  uint64_t sp_ = 0;
  uint64_t ip_ = 0;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_SYSCALL_H__
