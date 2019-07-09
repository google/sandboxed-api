// Copyright 2019 Google LLC. All Rights Reserved.
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

#include "sandboxed_api/sandbox2/mounts.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/regs.h"
#include "sandboxed_api/sandbox2/unwind/unwind.pb.h"

namespace sandbox2 {

// Maximum depth of analyzed call stack.
constexpr size_t kDefaultMaxFrames = 200;

// Returns the stack-trace of the PID=pid, delimited by the delim argument.
std::string GetStackTrace(const Regs* regs, const Mounts& mounts,
                          const std::string& delim = " ");

// Similar to GetStackTrace() but without using the sandbox to isolate
// libunwind.
std::string UnsafeGetStackTrace(pid_t pid, const std::string& delim = " ");

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_STACK_TRACE_H_
