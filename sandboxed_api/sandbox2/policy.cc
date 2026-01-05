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

// Implementation of the sandbox2::Policy class.

#include "sandboxed_api/sandbox2/policy.h"

#include <fcntl.h>
#include <linux/audit.h>
#include <linux/bpf_common.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sched.h>
#include <sys/mman.h>
#include <syscall.h>

#include <cerrno>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/bpfdisassembler.h"
#include "sandboxed_api/sandbox2/flags.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/sandbox2/util/seccomp_unotify.h"

#ifndef BPF_MAP_LOOKUP_ELEM
#define BPF_MAP_LOOKUP_ELEM 1
#endif
#ifndef BPF_OBJ_GET
#define BPF_OBJ_GET 7
#endif
#ifndef BPF_MAP_GET_NEXT_KEY
#define BPF_MAP_GET_NEXT_KEY 4
#endif
#ifndef BPF_MAP_GET_NEXT_ID
#define BPF_MAP_GET_NEXT_ID 12
#endif
#ifndef BPF_MAP_GET_FD_BY_ID
#define BPF_MAP_GET_FD_BY_ID 14
#endif
#ifndef BPF_OBJ_GET_INFO_BY_FD
#define BPF_OBJ_GET_INFO_BY_FD 15
#endif

#ifndef CLONE_NEWCGROUP
#define CLONE_NEWCGROUP 0x02000000
#endif

#ifndef SECCOMP_FILTER_FLAG_NEW_LISTENER
#define SECCOMP_FILTER_FLAG_NEW_LISTENER (1UL << 3)
#endif

namespace sandbox2 {

// The final policy is the concatenation of:
//   1. default policy (GetDefaultPolicy, private),
//   2. user policy (user_policy_, public),
//   3. default KILL action (avoid failing open if user policy did not do it).
std::vector<sock_filter> Policy::GetPolicy(
    bool user_notif, bool enable_sandboxing_pre_execve) const {
  if (absl::GetFlag(FLAGS_sandbox2_danger_danger_permit_all) ||
      !absl::GetFlag(FLAGS_sandbox2_danger_danger_permit_all_and_log).empty()) {
    return GetTrackingPolicy();
  }

  // Now we can start building the policy.
  // 1. Start with the default policy (e.g. syscall architecture checks).
  auto policy = GetDefaultPolicy(user_notif, enable_sandboxing_pre_execve);
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
std::vector<sock_filter> Policy::GetDefaultPolicy(
    bool user_notif, bool enable_sandboxing_pre_execve) const {
  bpf_labels l = {0};

  std::vector<sock_filter> policy;
  if (user_notif) {
    sock_filter execve_action = ALLOW;
    if (util::SeccompUnotify::IsContinueSupported()) {
      execve_action = BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_USER_NOTIF);
    }
    policy = {
        // If compiled arch is different from the runtime one, inform the
        // Monitor.
        LOAD_ARCH,
        JNE32(Syscall::GetHostAuditArch(), DENY),
        LOAD_SYSCALL_NR,
        JNE32(__NR_seccomp, JUMP(&l, past_seccomp_l)),
        ARG_32(3),
        JNE32(internal::kExecveMagic, JUMP(&l, past_seccomp_l)),
        ALLOW,
        LABEL(&l, past_seccomp_l),
        LOAD_SYSCALL_NR,
    };
    if (enable_sandboxing_pre_execve) {
      policy.insert(
          policy.end(),
          {
              JNE32(__NR_execveat, JUMP(&l, past_execveat_l)),
              ARG_32(4),
              JNE32(AT_EMPTY_PATH, JUMP(&l, past_execveat_l)),
              ARG_32(5),
              JNE32(internal::kExecveMagic, JUMP(&l, past_execveat_l)),
              execve_action,
              LABEL(&l, past_execveat_l),
              LOAD_SYSCALL_NR,
          });
    }
  } else {
    policy = {
        // If compiled arch is different from the runtime one, inform the
        // Monitor.
        LOAD_ARCH,
#if defined(SAPI_X86_64)
        JEQ32(AUDIT_ARCH_I386, TRACE(sapi::cpu::kX86)),  // 32-bit sandboxee
#endif
        JNE32(Syscall::GetHostAuditArch(), TRACE(sapi::cpu::kUnknown)),

        LOAD_SYSCALL_NR,
    };
    if (enable_sandboxing_pre_execve) {
      // After the policy is uploaded, forkserver will execve the sandboxee.
      // We need to allow this execve but not others. Since BPF does not have
      // state, we need to inform the Monitor to decide, and for that we use a
      // magic value in syscall args 5. Note that this value is not supposed
      // to be secret, but just an optimization so that the monitor is not
      // triggered on every call to execveat.
      policy.insert(
          policy.end(),
          {
              JNE32(__NR_execveat, JUMP(&l, past_execveat_l)),
              ARG_32(4),
              JNE32(AT_EMPTY_PATH, JUMP(&l, past_execveat_l)),
              ARG_32(5),
              JNE32(internal::kExecveMagic, JUMP(&l, past_execveat_l)),
              SANDBOX2_TRACE,
              LABEL(&l, past_execveat_l),
              LOAD_SYSCALL_NR,
          });
    }
  }

  // Insert a custom syscall to signal the sandboxee it's running inside a
  // sandbox.
  // Executing a syscall with ID util::kMagicSyscallNo will return
  // util::kMagicSyscallErr when the call by the sandboxee code is made inside
  // the sandbox and ENOSYS when it is not inside the sandbox.
  policy.insert(policy.end(), {SYSCALL(internal::kMagicSyscallNo,
                                       ERRNO(internal::kMagicSyscallErr))});

  // Forbid ptrace because it's unsafe or too risky. The user policy can only
  // block (i.e. return an error instead of killing the process) but not allow
  // ptrace. This uses LOAD_SYSCALL_NR from above.
  if (!user_policy_handles_ptrace_) {
    policy.insert(policy.end(), {JEQ32(__NR_ptrace, DENY)});
  }

  // If user policy doesn't mention it, forbid bpf() because it's unsafe or too
  // risky. Users can still allow safe invocations of this syscall by using
  // PolicyBuilder::AllowSafeBpf(). This uses LOAD_SYSCALL_NR from above.
    if (allow_safe_bpf_) {
      policy.insert(policy.end(), {
                                      JNE32(__NR_bpf, JUMP(&l, past_bpf_l)),
                                      ARG_32(0),
                                      JEQ32(BPF_MAP_LOOKUP_ELEM, ALLOW),
                                      JEQ32(BPF_OBJ_GET, ALLOW),
                                      JEQ32(BPF_MAP_GET_NEXT_KEY, ALLOW),
                                      JEQ32(BPF_MAP_GET_NEXT_ID, ALLOW),
                                      JEQ32(BPF_MAP_GET_FD_BY_ID, ALLOW),
                                      JEQ32(BPF_OBJ_GET_INFO_BY_FD, ALLOW),
                                      LABEL(&l, past_bpf_l),
                                      LOAD_SYSCALL_NR,
                                  });
    }
    if (!user_policy_handles_bpf_) {
      policy.insert(policy.end(), {JEQ32(__NR_bpf, DENY)});
    }

  if (!allow_map_exec_) {
    policy.insert(
        policy.end(),
        {
    // TODO: b/453946404 - The below checks are not correct.
#ifdef __NR_mmap
            JNE32(__NR_mmap, JUMP(&l, past_map_exec_l)),
#endif
#ifdef __NR_mmap2  // Arm32
            JNE32(__NR_mmap2, JUMP(&l, past_map_exec_l)),
#endif
            JNE32(__NR_mprotect, JUMP(&l, past_map_exec_l)),
#ifdef __NR_pkey_mprotect
            JNE32(__NR_pkey_mprotect, JUMP(&l, past_map_exec_l)),
#endif
            // Load "prot" argument, which is the same for all four syscalls.
            ARG_32(2),
            // Deny executable mappings. This also disallows them for all PKEYS
            // (not just the default one).
            JA32(PROT_EXEC, DENY),

            LABEL(&l, past_map_exec_l),
            LOAD_SYSCALL_NR,
        });
  }

  constexpr uintptr_t kNewNamespacesFlags =
      CLONE_NEWNS | CLONE_NEWUSER | CLONE_NEWNET | CLONE_NEWUTS |
      CLONE_NEWCGROUP | CLONE_NEWIPC | CLONE_NEWPID;
  static_assert(kNewNamespacesFlags <= std::numeric_limits<uint32_t>::max());

  static_assert(CLONE_UNTRACED <= std::numeric_limits<uint32_t>::max());
  // For unotify monitor tracing is not used for policy enforcement, thus it's
  // fine to allow CLONE_UNTRACED.
  const uint32_t unsafe_clone_flags =
      kNewNamespacesFlags | (user_notif ? 0 : CLONE_UNTRACED);
  policy.insert(policy.end(),
                {
#ifdef __NR_clone3
                    // Disallow clone3. Errno instead of DENY so that libraries
                    // can fallback to regular clone/clone2.
                    JEQ32(__NR_clone3, ERRNO(ENOSYS)),
#endif
                    // Disallow clone3 and clone with unsafe flags.  This uses
                    // LOAD_SYSCALL_NR from above.
                    JNE32(__NR_clone, JUMP(&l, past_clone_unsafe_l)),
                    // Regardless of arch, we only care about the lower 32-bits
                    // of the flags.
                    ARG_32(0),
                    JA32(unsafe_clone_flags, DENY),
                    LABEL(&l, past_clone_unsafe_l),
                    // Disallow unshare with unsafe flags.
                    LOAD_SYSCALL_NR,
                    JNE32(__NR_unshare, JUMP(&l, past_unshare_unsafe_l)),
                    // Regardless of arch, we only care about the lower 32-bits
                    // of the flags.
                    ARG_32(0),
                    JA32(kNewNamespacesFlags, DENY),
                    LABEL(&l, past_unshare_unsafe_l),
                    // Disallow seccomp with SECCOMP_FILTER_FLAG_NEW_LISTENER
                    // flag.
                    LOAD_SYSCALL_NR,
                    JNE32(__NR_seccomp, JUMP(&l, past_seccomp_new_listener)),
                    // Regardless of arch, we only care about the lower 32-bits
                    // of the flags.
                    ARG_32(1),
                    JA32(SECCOMP_FILTER_FLAG_NEW_LISTENER, DENY),
                    LABEL(&l, past_seccomp_new_listener),
                });

  if (bpf_resolve_jumps(&l, policy.data(), policy.size()) != 0) {
    LOG(FATAL) << "Cannot resolve bpf jumps";
  }

  return policy;
}

std::vector<sock_filter> Policy::GetTrackingPolicy() const {
  return {
      LOAD_ARCH,
      BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, Syscall::GetHostAuditArch(), 0, 3),
      LOAD_SYSCALL_NR,
      BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, internal::kMagicSyscallNo, 0, 1),
      ERRNO(internal::kMagicSyscallErr),
      LOAD_ARCH,
#if defined(SAPI_X86_64)
      JEQ32(AUDIT_ARCH_X86_64, TRACE(sapi::cpu::kX8664)),
      JEQ32(AUDIT_ARCH_I386, TRACE(sapi::cpu::kX86)),
#elif defined(SAPI_PPC64_LE)
      JEQ32(AUDIT_ARCH_PPC64LE, TRACE(sapi::cpu::kPPC64LE)),
#elif defined(SAPI_ARM64)
      JEQ32(AUDIT_ARCH_AARCH64, TRACE(sapi::cpu::kArm64)),
#elif defined(SAPI_ARM)
      JEQ32(AUDIT_ARCH_ARM, TRACE(sapi::cpu::kArm)),
#endif
      TRACE(sapi::cpu::kUnknown),
  };
}

}  // namespace sandbox2
