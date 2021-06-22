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

// The sandbox2::StackTrace class provides static methods useful when analyzing
// call-stack of the process. It uses libunwind-ptrace, so the process must be
// in a stopped state to call those methods.

#ifndef SANDBOXED_API_SANDBOX2_STACK_TRACE_H_
#define SANDBOXED_API_SANDBOX2_STACK_TRACE_H_

#include <sys/types.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "sandboxed_api/sandbox2/mounts.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/regs.h"

namespace sandbox2 {

// Maximum depth of analyzed call stack.
constexpr size_t kDefaultMaxFrames = 200;

// Returns the stack-trace of the PID=pid, one line per frame.
std::vector<std::string> GetStackTrace(const Regs* regs, const Mounts& mounts);

// Similar to GetStackTrace() but without using the sandbox to isolate
// libunwind.
std::vector<std::string> UnsafeGetStackTrace(pid_t pid);

// Returns a stack trace that collapses duplicate stack frames and annotates
// them with a repetition count.
// Example:
//   _start              _start
//   main                main
//   recursive_call      recursive_call
//   recursive_call      (previous frame repeated 2 times)
//   recursive_call      tail_call
//   tail_call
std::vector<std::string> CompactStackTrace(
    const std::vector<std::string>& stack_trace);

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_STACK_TRACE_H_
