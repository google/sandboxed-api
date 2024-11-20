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

#include <linux/bpf_common.h>
#include <linux/filter.h>   // IWYU pragma: export
#include <linux/seccomp.h>  // IWYU pragma: export

#include <cstdint>
#include <optional>
#include <vector>

#include "sandboxed_api/sandbox2/namespace.h"
#include "sandboxed_api/sandbox2/network_proxy/filtering.h"
#include "sandboxed_api/sandbox2/syscall.h"  // IWYU pragma: export

#define SANDBOX2_TRACE         \
  BPF_STMT(BPF_RET + BPF_K,    \
           SECCOMP_RET_TRACE | \
               (::sandbox2::Syscall::GetHostArch() & SECCOMP_RET_DATA))

namespace sandbox2 {

namespace internal {
// Magic values of registers when executing sys_execveat, so we can recognize
// the pre-sandboxing state and notify the Monitor
inline constexpr uintptr_t kExecveMagic = 0x921c2c34;
}  // namespace internal

class MonitorBase;
class PolicyBuilder;

class Policy final {
 public:
  Policy(const Policy&) = default;
  Policy& operator=(const Policy&) = default;

  Policy(Policy&&) = delete;
  Policy& operator=(Policy&&) = delete;

  // Returns the policy, but modifies it according to FLAGS and internal
  // requirements (message passing via Comms, Executor::WaitForExecve etc.).
  std::vector<sock_filter> GetPolicy(bool user_notif) const;

  const std::optional<Namespace>& GetNamespace() const { return namespace_; }
  const Namespace* GetNamespaceOrNull() const {
    return namespace_ ? &namespace_.value() : nullptr;
  }

  // Returns the default policy, which blocks certain dangerous syscalls and
  // mismatched syscall tables.
  std::vector<sock_filter> GetDefaultPolicy(bool user_notif) const;
  // Returns a policy allowing the Monitor module to track all syscalls.
  std::vector<sock_filter> GetTrackingPolicy() const;

  bool collect_stacktrace_on_signal() const {
    return collect_stacktrace_on_signal_;
  }

  bool collect_stacktrace_on_exit() const {
    return collect_stacktrace_on_exit_;
  }

 private:
  friend class PolicyBuilder;
  friend class MonitorBase;

  // Private constructor only called by the PolicyBuilder.
  Policy() = default;

  // The Namespace object, defines ways of putting sandboxee into namespaces.
  std::optional<Namespace> namespace_;

  // Gather stack traces on violations, signals, timeouts or when getting
  // killed. See policybuilder.h for more information.
  bool collect_stacktrace_on_violation_ = true;
  bool collect_stacktrace_on_signal_ = true;
  bool collect_stacktrace_on_timeout_ = true;
  bool collect_stacktrace_on_kill_ = true;
  bool collect_stacktrace_on_exit_ = false;

  bool allow_speculation_ = false;

  // The policy set by the user.
  std::vector<sock_filter> user_policy_;
  bool user_policy_handles_bpf_ = false;
  bool user_policy_handles_ptrace_ = false;

  // Contains a list of hosts the sandboxee is allowed to connect to.
  std::optional<AllowedHosts> allowed_hosts_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_POLICY_H_
