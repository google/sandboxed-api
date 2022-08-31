// Copyright 2020 Google LLC
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

#ifndef SANDBOXED_API_CONFIG_H_
#define SANDBOXED_API_CONFIG_H_

#include <cstdint>
#include <string>

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

// 32-bit ARM
#elif defined(__arm__) || defined(_M_ARM)
#define SAPI_ARM 1

#endif

namespace sapi {

namespace cpu {

// CPU architectures known to Sandbox2
enum Architecture : uint16_t {
  // Linux: Use a magic value, so it can be easily spotted in the seccomp-bpf
  // bytecode decompilation stream. Must be < (1<<15), as that is the size of
  // data which can be returned by BPF.
  kUnknown = 0xCAF0,
  kX8664,
  kX86,
  kPPC64LE,
  kArm64,
  kArm,
  kMax = kArm
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
#elif defined(SAPI_ARM64)
  return cpu::kArm64;
#elif defined(SAPI_ARM)
  return cpu::kArm;
#else
  return cpu::kUnknown;
#endif
}

constexpr bool IsX8664() { return Architecture() == cpu::kX8664; }

constexpr bool IsPPC64LE() { return Architecture() == cpu::kPPC64LE; }

constexpr bool IsArm64() { return Architecture() == cpu::kArm64; }

constexpr bool IsArm() { return Architecture() == cpu::kArm; }

constexpr bool Is64Bit() { return sizeof(uintptr_t) == 8; }

}  // namespace host_cpu

static_assert(host_cpu::Architecture() != cpu::kUnknown,
              "Host CPU architecture is not supported: One of x86-64, POWER64 "
              "(little endian), ARM or AArch64 is required.");

namespace os {

// Operating Systems known to Sandbox2
enum Platform : uint16_t {
  kUnknown,
  kAndroid,
  kLinux,
};

}  // namespace os

namespace host_os {

// Returns the current host OS platform if supported. If not supported,
// returns platforms::kUnknown.
constexpr os::Platform Platform() {
#if defined(__ANDROID__)
  return os::kAndroid;
#elif defined(__linux__)
  return os::kLinux;
#else
  return os::kUnknown;
#endif
}

constexpr bool IsAndroid() { return Platform() == os::kAndroid; }

constexpr bool IsLinux() { return Platform() == os::kLinux; }

}  // namespace host_os

namespace sanitizers {

constexpr bool IsMSan() {
#ifdef ABSL_HAVE_MEMORY_SANITIZER
  return true;
#else
  return false;
#endif
}

constexpr bool IsTSan() {
#ifdef ABSL_HAVE_THREAD_SANITIZER
  return true;
#else
  return false;
#endif
}

constexpr bool IsASan() {
#ifdef ABSL_HAVE_ADDRESS_SANITIZER
  return true;
#else
  return false;
#endif
}

constexpr bool IsHwASan() {
#ifdef ABSL_HAVE_HWADDRESS_SANITIZER
  return true;
#else
  return false;
#endif
}

constexpr bool IsLSan() {
#ifdef ABSL_HAVE_LEAK_SANITIZER
  return true;
#else
  return false;
#endif
}

// Returns whether any of the sanitizers is enabled.
constexpr bool IsAny() {
  return IsMSan() || IsTSan() || IsASan() || IsHwASan() || IsLSan();
}

}  // namespace sanitizers

}  // namespace sapi

#endif  // SANDBOXED_API_CONFIG_H_
