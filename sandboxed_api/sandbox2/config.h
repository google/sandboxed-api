// Copyright 2020 Google LLC
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

#ifndef SANDBOXED_API_SANDBOX2_CONFIG_H_
#define SANDBOXED_API_SANDBOX2_CONFIG_H_

#include <cstdint>

#include "absl/base/config.h"

// GCC/Clang define __x86_64__, Visual Studio uses _M_X64
#if defined(__x86_64__) || defined(_M_X64)
#define SAPI_X86_64 1

// Check various spellings for 64-bit POWER. Not checking for Visual Studio, as
// it does not support 64-bit POWER.
#elif (defined(__PPC64__) || defined(__powerpc64__) || defined(__ppc64__)) && \
    defined(ABSL_IS_LITTLE_ENDIAN)
#define SAPI_PPC64_LE 1

// Spellings for AArch64
#elif defined(__aarch64__) || defined(_M_ARM64)
#define SAPI_ARM64 1
#endif

namespace sandbox2 {

namespace cpu {

// CPU architectures known to Sandbox2
enum Architecture : uint16_t {
  // Linux: Use a magic value, so it can be easily spotted in the seccomp-bpf
  // bytecode decompilation stream. Must be < (1<<15), as/ that's the size of
  // data which can be returned by BPF.
  kUnknown = 0xCAF0,
  kX8664,
  kX86,
  kPPC64LE,
  kArm64,
};

}  // namespace cpu

namespace host_cpu {

// Returns the current host CPU architecture if supported. If not supported,
// returns cpu::kUnknown.
constexpr cpu::Architecture Architecture() {
#if defined(SAPI_X86_64)
  return cpu::kX8664;
#elif defined(SAPI_PPC64_LE)
  return cpu::kPPC64LE;
#else
  return cpu::kUnknown;
#endif
}

constexpr bool IsX8664() { return Architecture() == cpu::kX8664; }

constexpr bool IsPPC64LE() { return Architecture() == cpu::kPPC64LE; }

constexpr bool IsArm64() { return Architecture() == cpu::kArm64; }

}  // namespace host_cpu

static_assert(host_cpu::Architecture() != cpu::kUnknown,
              "Host CPU architecture is not supported: One of x86-64 or "
              "POWER64 (little endian) is required.");

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_CONFIG_H_