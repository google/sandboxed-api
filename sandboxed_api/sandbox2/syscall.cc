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

#include "sandboxed_api/sandbox2/syscall.h"

#include <linux/audit.h>
#include <linux/elf-em.h>

#include <climits>
#include <csignal>
#include <cstring>
#include <string>

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/syscall_defs.h"

#ifndef AUDIT_ARCH_PPC64LE
#define AUDIT_ARCH_PPC64LE (EM_PPC64 | __AUDIT_ARCH_64BIT | __AUDIT_ARCH_LE)
#endif

namespace sandbox2 {

std::string Syscall::GetArchDescription(sapi::cpu::Architecture arch) {
  switch (arch) {
    case sapi::cpu::kX8664:
      return "[X86-64]";
    case sapi::cpu::kX86:
      return "[X86-32]";
    case sapi::cpu::kPPC64LE:
      return "[PPC-64]";
    case sapi::cpu::kArm64:
      return "[Arm-64]";
    case sapi::cpu::kArm:
      return "[Arm-32]";
    default:
      return absl::StrFormat("[UNKNOWN_ARCH:%d]", arch);
  }
}

uint32_t Syscall::GetHostAuditArch() {
  switch (sapi::host_cpu::Architecture()) {
    case sapi::cpu::kX8664:
      return AUDIT_ARCH_X86_64;
    case sapi::cpu::kPPC64LE:
      return AUDIT_ARCH_PPC64LE;
    case sapi::cpu::kArm64:
      return AUDIT_ARCH_AARCH64;
    case sapi::cpu::kArm:
      return AUDIT_ARCH_ARM;
    default:
      // The static_assert() in config.h should prevent us from ever getting
      // here.
      return 0;  // Not reached
  }
}

std::string Syscall::GetName() const {
  if (absl::string_view name = SyscallTable::get(arch_).GetName(nr_);
      !name.empty()) {
    return std::string(name);
  }
  return absl::StrFormat("UNKNOWN[%d/0x%x]", nr_, nr_);
}

std::vector<std::string> Syscall::GetArgumentsDescription() const {
  return SyscallTable::get(arch_).GetArgumentsDescription(nr_, args_.data(),
                                                          pid_);
}

std::string Syscall::GetDescription() const {
  const std::string arch = GetArchDescription(arch_);
  const std::string args = absl::StrJoin(GetArgumentsDescription(), ", ");
  return absl::StrFormat("%s %s [%d](%s) IP: %#x, STACK: %#x", arch, GetName(),
                         nr_, args, ip_, sp_);
}

}  // namespace sandbox2
