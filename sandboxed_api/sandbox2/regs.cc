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

// Implementation of the sandbox2::Regs class.

#include "sandboxed_api/sandbox2/regs.h"

#include <elf.h>  // IWYU pragma: keep // used for NT_PRSTATUS inside an ifdef
#include <linux/audit.h>
#include <sys/ptrace.h>
#include <sys/uio.h>  // IWYU pragma: keep // used for iovec

#include <cerrno>

#include "absl/strings/str_cat.h"
#include "sandboxed_api/sandbox2/util/strerror.h"
#include "sandboxed_api/util/canonical_errors.h"

namespace sandbox2 {

sapi::Status Regs::Fetch() {
#if defined(__powerpc64__)
  iovec pt_iov = {&user_regs_, sizeof(user_regs_)};

  if (ptrace(PTRACE_GETREGSET, pid_, NT_PRSTATUS, &pt_iov) == -1L) {
    return sapi::InternalError(absl::StrCat(
        "ptrace(PTRACE_GETREGSET, pid=", pid_, ") failed: ", StrError(errno)));
  }
  if (pt_iov.iov_len != sizeof(user_regs_)) {
    return sapi::InternalError(absl::StrCat(
        "ptrace(PTRACE_GETREGSET, pid=", pid_,
        ") size returned: ", pt_iov.iov_len,
        " different than sizeof(user_regs_): ", sizeof(user_regs_)));
  }
#else
  if (ptrace(PTRACE_GETREGS, pid_, 0, &user_regs_) == -1L) {
    return sapi::InternalError(absl::StrCat("ptrace(PTRACE_GETREGS, pid=", pid_,
                                            ") failed: ", StrError(errno)));
  }
#endif
  return sapi::OkStatus();
}

sapi::Status Regs::Store() {
#if defined(__powerpc64__)
  iovec pt_iov = {&user_regs_, sizeof(user_regs_)};

  if (ptrace(PTRACE_SETREGSET, pid_, NT_PRSTATUS, &pt_iov) == -1L) {
    return sapi::InternalError(absl::StrCat(
        "ptrace(PTRACE_SETREGSET, pid=", pid_, ") failed: ", StrError(errno)));
  }
#else
  if (ptrace(PTRACE_SETREGS, pid_, 0, &user_regs_) == -1) {
    return sapi::InternalError(absl::StrCat("ptrace(PTRACE_SETREGS, pid=", pid_,
                                            ") failed: ", StrError(errno)));
  }
#endif
  return sapi::OkStatus();
}

sapi::Status Regs::SkipSyscallReturnValue(uint64_t value) {
#if defined(__x86_64__)
  user_regs_.orig_rax = -1;
  user_regs_.rax = value;
#elif defined(__powerpc64__)
  user_regs_.gpr[0] = -1;
  user_regs_.gpr[3] = value;
#endif
  return Store();
}

Syscall Regs::ToSyscall(Syscall::CpuArch syscall_arch) const {
#if defined(__x86_64__)
  if (ABSL_PREDICT_TRUE(syscall_arch == Syscall::kX86_64)) {
    auto syscall = user_regs_.orig_rax;
    Syscall::Args args = {user_regs_.rdi, user_regs_.rsi, user_regs_.rdx,
                          user_regs_.r10, user_regs_.r8,  user_regs_.r9};
    auto sp = user_regs_.rsp;
    auto ip = user_regs_.rip;
    return Syscall(syscall_arch, syscall, args, pid_, sp, ip);
  }
  if (syscall_arch == Syscall::kX86_32) {
    auto syscall = user_regs_.orig_rax & 0xFFFFFFFF;
    Syscall::Args args = {
        user_regs_.rbx & 0xFFFFFFFF, user_regs_.rcx & 0xFFFFFFFF,
        user_regs_.rdx & 0xFFFFFFFF, user_regs_.rsi & 0xFFFFFFFF,
        user_regs_.rdi & 0xFFFFFFFF, user_regs_.rbp & 0xFFFFFFFF};
    auto sp = user_regs_.rsp & 0xFFFFFFFF;
    auto ip = user_regs_.rip & 0xFFFFFFFF;
    return Syscall(syscall_arch, syscall, args, pid_, sp, ip);
  }
#elif defined(__powerpc64__)
  if (ABSL_PREDICT_TRUE(syscall_arch == Syscall::kPPC_64)) {
    auto syscall = user_regs_.gpr[0];
    Syscall::Args args = {user_regs_.orig_gpr3, user_regs_.gpr[4],
                          user_regs_.gpr[5],    user_regs_.gpr[6],
                          user_regs_.gpr[7],    user_regs_.gpr[8]};
    auto sp = user_regs_.gpr[1];
    auto ip = user_regs_.nip;
    return Syscall(syscall_arch, syscall, args, pid_, sp, ip);
  }
#endif
  return Syscall(pid_);
}

void Regs::StoreRegisterValuesInProtobuf(RegisterValues* values) const {
#if defined(__x86_64__)
  RegisterX8664* regs = values->mutable_register_x86_64();
  regs->set_r15(user_regs_.r15);
  regs->set_r14(user_regs_.r14);
  regs->set_r13(user_regs_.r13);
  regs->set_r12(user_regs_.r12);
  regs->set_rbp(user_regs_.rbp);
  regs->set_rbx(user_regs_.rbx);
  regs->set_r11(user_regs_.r11);
  regs->set_r10(user_regs_.r10);
  regs->set_r9(user_regs_.r9);
  regs->set_r8(user_regs_.r8);
  regs->set_rax(user_regs_.rax);
  regs->set_rcx(user_regs_.rcx);
  regs->set_rdx(user_regs_.rdx);
  regs->set_rsi(user_regs_.rsi);
  regs->set_rdi(user_regs_.rdi);
  regs->set_orig_rax(user_regs_.orig_rax);
  regs->set_rip(user_regs_.rip);
  regs->set_cs(user_regs_.cs);
  regs->set_eflags(user_regs_.eflags);
  regs->set_rsp(user_regs_.rsp);
  regs->set_ss(user_regs_.ss);
  regs->set_fs_base(user_regs_.fs_base);
  regs->set_gs_base(user_regs_.gs_base);
  regs->set_ds(user_regs_.ds);
  regs->set_es(user_regs_.es);
  regs->set_fs(user_regs_.fs);
  regs->set_gs(user_regs_.gs);
#elif defined(__powerpc64__)
  RegisterPowerpc64* regs = values->mutable_register_powerpc64();
  for (int i = 0; i < ABSL_ARRAYSIZE(user_regs_.gpr); ++i) {
    regs->add_gpr(user_regs_.gpr[i]);
  }
  regs->set_nip(user_regs_.nip);
  regs->set_msr(user_regs_.msr);
  regs->set_orig_gpr3(user_regs_.orig_gpr3);
  regs->set_ctr(user_regs_.ctr);
  regs->set_link(user_regs_.link);
  regs->set_xer(user_regs_.xer);
  regs->set_ccr(user_regs_.ccr);
  regs->set_softe(user_regs_.softe);
  regs->set_trap(user_regs_.trap);
  regs->set_dar(user_regs_.dar);
  regs->set_dsisr(user_regs_.dsisr);
  regs->set_result(user_regs_.result);
  regs->set_zero0(user_regs_.zero0);
  regs->set_zero1(user_regs_.zero1);
  regs->set_zero2(user_regs_.zero2);
  regs->set_zero3(user_regs_.zero3);
#endif
}

}  // namespace sandbox2
