// Copyright 2022 Google LLC
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

#ifndef SANDBOXED_API_SANDBOX2_UTIL_SYSCALL_TRAP_H_
#define SANDBOXED_API_SANDBOX2_UTIL_SYSCALL_TRAP_H_

#include <signal.h>

#include <array>

namespace sandbox2 {

// Helper class for intercepting syscalls via SECCCOMP_RET_TRAP.
class SyscallTrap {
 public:
  static constexpr int kSyscallArgs = 6;
  using Args = std::array<uintptr_t, kSyscallArgs>;

  // Installs the syscall trap handler.
  // Returns false if the handler could not be installed.
  static bool Install(bool (*handler)(int nr, Args args, uintptr_t* result));

 private:
  static void SignalHandler(int nr, siginfo_t* info, void* context);

  explicit SyscallTrap(bool (*handler)(int nr, Args args, uintptr_t* result))
      : handler_(handler) {}
  void InvokeOldAct(int nr, siginfo_t* info, void* context);
  void SignalHandlerImpl(int nr, siginfo_t* info, void* context);

  struct sigaction oldact_;
  bool (*handler_)(int nr, Args args, uintptr_t* result);
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_UTIL_SYSCALL_TRAP_H_
