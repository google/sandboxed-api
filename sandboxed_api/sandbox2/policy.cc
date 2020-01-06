// Copyright 2020 Google LLC. All Rights Reserved.
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

// Implementation of the sandbox2::Policy class.

#include "sandboxed_api/sandbox2/policy.h"

#include <fcntl.h>
#include <linux/audit.h>
#include <linux/ipc.h>
#include <sched.h>
#include <sys/mman.h>
#include <syscall.h>

#include <cstring>
#include <string>

#include <glog/logging.h>
#include "sandboxed_api/util/flag.h"
#include "libcap/include/sys/capability.h"
#include "sandboxed_api/sandbox2/bpfdisassembler.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/regs.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"

ABSL_FLAG(bool, sandbox2_danger_danger_permit_all, false,
          "Allow all syscalls, useful for testing");
ABSL_FLAG(string, sandbox2_danger_danger_permit_all_and_log, "",
          "Allow all syscalls and log them into specified file");

namespace sandbox2 {

// The final policy is the concatenation of:
//   1. default policy (GetDefaultPolicy, private),
//   2. user policy (user_policy_, public),
//   3. default KILL action (avoid failing open if user policy did not do it).
std::vector<sock_filter> Policy::GetPolicy() const {
  if (absl::GetFlag(FLAGS_sandbox2_danger_danger_permit_all) ||
      !absl::GetFlag(FLAGS_sandbox2_danger_danger_permit_all_and_log).empty()) {
    return GetTrackingPolicy();
  }

  // Now we can start building the policy.
  // 1. Start with the default policy (e.g. syscall architecture checks).
  auto policy = GetDefaultPolicy();
  VLOG(3) << "Default policy:\n" << bpf::Disasm(policy);

  // 2. Append user policy.
  VLOG(3) << "User policy:\n" << bpf::Disasm(user_policy_);
  // Add default syscall_nr loading in case the user forgets.
  policy.push_back(LOAD_SYSCALL_NR);
  policy.insert(policy.end(), user_policy_.begin(), user_policy_.end());

  // 3. Finish with default KILL action.
  policy.push_back(KILL);

  VLOG(2) << "Final policy:\n" << bpf::Disasm(policy);
  return policy;
}

// If you modify this function, you should also modify.
// Monitor::LogAccessViolation to keep them in sync.
//
// Produces a policy which returns SECCOMP_RET_TRACE instead of SECCOMP_RET_KILL
// for the __NR_execve syscall, so the tracer can make a decision to allow or
// disallow it depending on which occurrence of __NR_execve it was.
// LINT.IfChange
std::vector<sock_filter> Policy::GetDefaultPolicy() const {
  bpf_labels l = {0};

  std::vector<sock_filter> policy = {
      // If compiled arch is different than the runtime one, inform the Monitor.
      LOAD_ARCH,
      JEQ32(Syscall::GetHostAuditArch(), JUMP(&l, past_arch_check_l)),
      JEQ32(AUDIT_ARCH_X86_64, TRACE(Syscall::kX86_64)),
      JEQ32(AUDIT_ARCH_I386, TRACE(Syscall::kX86_32)),
      JEQ32(AUDIT_ARCH_PPC64LE, TRACE(Syscall::kPPC_64)),
      TRACE(Syscall::kUnknown),
      LABEL(&l, past_arch_check_l),

      // After the policy is uploaded, forkserver will execve the sandboxee. We
      // need to allow this execve but not others. Since BPF does not have
      // state, we need to inform the Monitor to decide, and for that we use a
      // magic value in syscall args 5. Note that this value is not supposed to
      // be secret, but just an optimization so that the monitor is not
      // triggered on every call to execveat.
      LOAD_SYSCALL_NR,
      JNE32(__NR_execveat, JUMP(&l, past_execveat_l)),
      ARG_32(4),
      JNE32(AT_EMPTY_PATH, JUMP(&l, past_execveat_l)),
      ARG_32(5),
      JNE32(internal::kExecveMagic, JUMP(&l, past_execveat_l)),
      SANDBOX2_TRACE,
      LABEL(&l, past_execveat_l),

      // Forbid some syscalls because unsafe or too risky.
      LOAD_SYSCALL_NR,
      JEQ32(__NR_ptrace, DENY),
      JEQ32(__NR_bpf, DENY),

      // Disallow clone with CLONE_UNTRACED flag.
      JNE32(__NR_clone, JUMP(&l, past_clone_untraced_l)),
      // Regardless of arch, we only care about the lower 32-bits of the flags.
      ARG_32(0),
      JA32(CLONE_UNTRACED, DENY),
      LABEL(&l, past_clone_untraced_l),
  };

  if (bpf_resolve_jumps(&l, policy.data(), policy.size()) != 0) {
    LOG(FATAL) << "Cannot resolve bpf jumps";
  }

  return policy;
}
// LINT.ThenChange(monitor.cc)

std::vector<sock_filter> Policy::GetTrackingPolicy() const {
  return {
      LOAD_ARCH,
      JEQ32(AUDIT_ARCH_X86_64, TRACE(Syscall::kX86_64)),
      JEQ32(AUDIT_ARCH_I386, TRACE(Syscall::kX86_32)),
      JEQ32(AUDIT_ARCH_PPC64LE, TRACE(Syscall::kPPC_64)),
      TRACE(Syscall::kUnknown),
  };
}

bool Policy::SendPolicy(Comms* comms) const {
  auto policy = GetPolicy();
  if (!comms->SendBytes(
          reinterpret_cast<uint8_t*>(policy.data()),
          static_cast<uint64_t>(policy.size()) * sizeof(sock_filter))) {
    LOG(ERROR) << "Couldn't send policy";
    return false;
  }

  return true;
}

void Policy::AllowUnsafeKeepCapabilities(
    std::unique_ptr<std::vector<cap_value_t>> caps) {
  if (namespace_) {
    namespace_->DisableUserNamespace();
  }
  capabilities_ = std::move(caps);
}

void Policy::GetPolicyDescription(PolicyDescription* policy) const {
  policy->set_user_bpf_policy(user_policy_.data(),
                              user_policy_.size() * sizeof(sock_filter));
  if (policy_builder_description_) {
    *policy->mutable_policy_builder_description() =
        *policy_builder_description_;
  }

  if (namespace_) {
    namespace_->GetNamespaceDescription(
        policy->mutable_namespace_description());
  }

  if (capabilities_) {
    for (const auto& cap : *capabilities_) {
      policy->add_capabilities(cap);
    }
  }
}

}  // namespace sandbox2
