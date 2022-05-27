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

// The sandbox2::Policy class provides methods for manipulating seccomp-bpf
// syscall policies.

#ifndef SANDBOXED_API_SANDBOX2_POLICY_H_
#define SANDBOXED_API_SANDBOX2_POLICY_H_

#include <asm/types.h>
#include <linux/filter.h>

#include <cstddef>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "absl/types/optional.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/namespace.h"
#include "sandboxed_api/sandbox2/network_proxy/filtering.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/sandbox2/violation.pb.h"

#define SANDBOX2_TRACE TRACE(::sandbox2::Syscall::GetHostArch())

namespace sandbox2 {

namespace internal {
// Magic values of registers when executing sys_execveat, so we can recognize
// the pre-sandboxing state and notify the Monitor
inline constexpr uintptr_t kExecveMagic = 0x921c2c34;
}  // namespace internal

class Comms;

class Policy final {
 public:
  // Skips creation of a user namespace and keep capabilities in the global
  // namespace. This only makes sense in some rare cases where the sandbox is
  // started as root, please talk to sandbox-team@ before using this function.
  void AllowUnsafeKeepCapabilities(std::vector<int> caps);

  // Stores information about the policy (and the policy builder if existing)
  // in the protobuf structure.
  void GetPolicyDescription(PolicyDescription* policy) const;

 private:
  friend class Monitor;
  friend class PolicyBuilder;
  friend class PolicyBuilderPeer;  // For testing
  friend class StackTracePeer;

  // Private constructor only called by the PolicyBuilder.
  Policy() = default;

  // Sends the policy over the IPC channel.
  bool SendPolicy(Comms* comms) const;

  // Returns the policy, but modifies it according to FLAGS and internal
  // requirements (message passing via Comms, Executor::WaitForExecve etc.).
  std::vector<sock_filter> GetPolicy() const;

  Namespace* GetNamespace() { return namespace_.get(); }
  void SetNamespace(std::unique_ptr<Namespace> ns) {
    namespace_ = std::move(ns);
  }

  const std::vector<int>& capabilities() const { return capabilities_; }

  // Returns the default policy, which blocks certain dangerous syscalls and
  // mismatched syscall tables.
  std::vector<sock_filter> GetDefaultPolicy() const;
  // Returns a policy allowing the Monitor module to track all syscalls.
  std::vector<sock_filter> GetTrackingPolicy() const;

  // The Namespace object, defines ways of putting sandboxee into namespaces.
  std::unique_ptr<Namespace> namespace_;

  // Gather stack traces on violations, signals, timeouts or when getting
  // killed. See policybuilder.h for more information.
  bool collect_stacktrace_on_violation_ = true;
  bool collect_stacktrace_on_signal_ = true;
  bool collect_stacktrace_on_timeout_ = true;
  bool collect_stacktrace_on_kill_ = true;
  bool collect_stacktrace_on_exit_ = false;

  // The capabilities to keep in the sandboxee.
  std::vector<int> capabilities_;

  // Optional pointer to a PolicyBuilder description pb object.
  std::unique_ptr<PolicyBuilderDescription> policy_builder_description_;

  // The policy set by the user.
  std::vector<sock_filter> user_policy_;
  bool user_policy_handles_bpf_ = false;
  bool user_policy_handles_ptrace_ = false;

  // Contains a list of hosts the sandboxee is allowed to connect to.
  absl::optional<AllowedHosts> allowed_hosts_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_POLICY_H_
