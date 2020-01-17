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

// Implementation of the sandbox2::Syscall class.

#include "sandboxed_api/sandbox2/syscall.h"

#include <linux/audit.h>
#include <linux/elf-em.h>
#include <climits>
#include <csignal>
#include <cstring>
#include <string>

#include <glog/logging.h>
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "sandboxed_api/sandbox2/syscall_defs.h"

#ifndef AUDIT_ARCH_PPC64LE
#define AUDIT_ARCH_PPC64LE (EM_PPC64 | __AUDIT_ARCH_64BIT | __AUDIT_ARCH_LE)
#endif

namespace sandbox2 {

std::string Syscall::GetArchDescription(CpuArch arch) {
  switch (arch) {
    case kX86_64:
      return "[X86-64]";
    case kX86_32:
      return "[X86-32]";
    case kPPC_64:
      return "[PPC-64]";
    default:
      LOG(ERROR) << "Unknown CPU architecture: " << arch;
      return absl::StrFormat("[UNKNOWN_ARCH:%d]", arch);
  }
}

Syscall::CpuArch Syscall::GetHostArch() {
#if defined(__x86_64__)
  return kX86_64;
#elif defined(__i386__)
  return kX86_32;
#elif defined(__powerpc64__)
  return kPPC_64;
#endif
}

uint32_t Syscall::GetHostAuditArch() {
#if defined(__x86_64__)
  return AUDIT_ARCH_X86_64;
#elif defined(__i386__)
  return AUDIT_ARCH_I386;
#elif defined(__powerpc64__)
  return AUDIT_ARCH_PPC64LE;
#endif
}

std::string Syscall::GetName() const {
  absl::string_view name = SyscallTable::get(arch_).GetName(nr_);
  if (name.empty()) {
    return absl::StrFormat("UNKNOWN[%d/0x%x]", nr_, nr_);
  }
  return std::string(name);
}

std::vector<std::string> Syscall::GetArgumentsDescription() const {
  return SyscallTable::get(arch_).GetArgumentsDescription(nr_, args_.data(),
                                                          pid_);
}

std::string Syscall::GetDescription() const {
  const auto& arch = GetArchDescription(arch_);
  const std::string args = absl::StrJoin(GetArgumentsDescription(), ", ");
  return absl::StrFormat("%s %s [%d](%s) IP: %#x, STACK: %#x", arch, GetName(),
                         nr_, args, ip_, sp_);
}

}  // namespace sandbox2
