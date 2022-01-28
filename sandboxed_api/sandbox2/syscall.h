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

// The sandbox2::Syscalls class defines mostly static helper methods which
// are used to analyze the status of the sandboxed process.

#ifndef SANDBOXED_API_SANDBOX2_SYSCALL_H__
#define SANDBOXED_API_SANDBOX2_SYSCALL_H__

#include <sys/types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "sandboxed_api/config.h"

namespace sandbox2 {

class Syscall {
 public:
  // Maximum number of syscall arguments
  static constexpr size_t kMaxArgs = 6;
  using Args = std::array<uint64_t, kMaxArgs>;

  // Returns the host architecture, according to CpuArch.
  static constexpr sapi::cpu::Architecture GetHostArch() {
    return sapi::host_cpu::Architecture();
  }

  // Returns the host architecture, according to <linux/audit.h>.
  static uint32_t GetHostAuditArch();

  // Returns a description of the architecture.
  static std::string GetArchDescription(sapi::cpu::Architecture arch);

  Syscall() = default;
  Syscall(sapi::cpu::Architecture arch, uint64_t nr, Args args = {})
      : arch_(arch), nr_(nr), args_(args) {}

  pid_t pid() const { return pid_; }
  uint64_t nr() const { return nr_; }
  sapi::cpu::Architecture arch() const { return arch_; }
  const Args& args() const { return args_; }
  uint64_t stack_pointer() const { return sp_; }
  uint64_t instruction_pointer() const { return ip_; }

  std::string GetName() const;

  std::vector<std::string> GetArgumentsDescription() const;
  std::string GetDescription() const;

 private:
  friend class Regs;

  explicit Syscall(pid_t pid) : pid_(pid) {}
  Syscall(sapi::cpu::Architecture arch, uint64_t nr, Args args, pid_t pid,
          uint64_t sp, uint64_t ip)
      : arch_(arch), nr_(nr), args_(args), pid_(pid), sp_(sp), ip_(ip) {}

  sapi::cpu::Architecture arch_ = sapi::cpu::kUnknown;
  uint64_t nr_ = -1;
  Args args_ = {};
  pid_t pid_ = -1;
  uint64_t sp_ = 0;
  uint64_t ip_ = 0;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_SYSCALL_H__
