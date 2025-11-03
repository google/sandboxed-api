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

// The sandbox2::StackTrace class provides static methods useful when analyzing
// call-stack of the process. It uses libunwind-ptrace, so the process must be
// in a stopped state to call those methods.

#ifndef SANDBOXED_API_SANDBOX2_STACK_TRACE_H_
#define SANDBOXED_API_SANDBOX2_STACK_TRACE_H_

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/namespace.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/regs.h"
#include "sandboxed_api/sandbox2/result.h"

namespace sandbox2 {

class Sandbox2;
class StackTraceTestPeer;

namespace internal {

class SandboxPeer {
 public:
  static std::unique_ptr<SandboxPeer> Spawn(std::unique_ptr<Executor> executor,
                                            std::unique_ptr<Policy> policy) {
    CHECK_NE(spawn_fn_, nullptr);
    return spawn_fn_(std::move(executor), std::move(policy));
  }

  virtual ~SandboxPeer() = default;

  virtual Comms* comms() = 0;
  virtual void Kill() = 0;
  virtual Result AwaitResult() = 0;

 private:
  friend class ::sandbox2::Sandbox2;
  friend class ::sandbox2::StackTraceTestPeer;
  using SpawnFn = std::unique_ptr<SandboxPeer> (*)(std::unique_ptr<Executor>,
                                                   std::unique_ptr<Policy>);
  static SpawnFn spawn_fn_;
};

}  // namespace internal

// Maximum depth of analyzed call stack.
constexpr size_t kDefaultMaxFrames = 200;

// Returns the stack-trace of the PID=pid, one line per frame.
absl::StatusOr<std::vector<std::string>> GetStackTrace(
    const Regs* regs, const Namespace* ns, bool uses_custom_forkserver,
    int recursion_depth);

absl::StatusOr<std::vector<std::pair<pid_t, std::vector<std::string>>>>
GetStackTracesForAllThreads(const std::vector<const Regs*>& regs,
                            const Namespace* ns, bool uses_custom_forkserver);

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
