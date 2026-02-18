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

#include "sandboxed_api/sandbox.h"

#include <sys/resource.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <syscall.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>

#include "absl/base/dynamic_annotations.h"
#include "absl/base/macros.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/sandbox2/limits.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/util/status_macros.h"
#include "sandboxed_api/var_abstract.h"
#include "sandboxed_api/var_array.h"
#include "sandboxed_api/var_int.h"
#include "sandboxed_api/var_ptr.h"
#include "sandboxed_api/var_reg.h"
#include "sandboxed_api/var_type.h"

namespace sapi {

// IMPORTANT: This policy must be safe to use with
// `Allow(sandbox2::UnrestrictedNetworking())`.
sandbox2::PolicyBuilder Sandbox2Config::DefaultPolicyBuilder() {
  sandbox2::PolicyBuilder builder;
  builder.AllowRead()
      .AllowWrite()
      .AllowExit()
      .AllowGetRlimit()
      .AllowGetIDs()
      .AllowTCGETS()
      .AllowTime()
      .AllowOpen()
      .AllowStat()
      .AllowHandleSignals()
      .AllowSystemMalloc()
      .AllowSafeFcntl()
      .AllowGetPIDs()
      .AllowSleep()
      .AllowReadlink()
      .AllowAccess()
      .AllowSharedMemory()
      .AllowSyscalls({
          __NR_recvmsg,
          __NR_sendmsg,
          __NR_futex,
          __NR_close,
          __NR_lseek,
          __NR_uname,
          __NR_kill,
          __NR_tgkill,
          __NR_tkill,
      });

#ifdef __NR_arch_prctl  // x86-64 only
  builder.AllowSyscall(__NR_arch_prctl);
#endif

  if constexpr (sanitizers::IsAny()) {
    LOG(WARNING) << "Allowing additional calls to support the LLVM "
                 << "(ASAN/MSAN/TSAN) sanitizer";
    builder.AllowLlvmSanitizers();
  }
  builder.AddFile("/etc/localtime")
      .AddTmpfs("/tmp", 1ULL << 30 /* 1GiB tmpfs (max size */);

  return builder;
}

sandbox2::Limits Sandbox2Config::DefaultLimits() {
  sandbox2::Limits limits;
  limits.set_rlimit_cpu(RLIM64_INFINITY);
  limits.set_walltime_limit(absl::ZeroDuration());
  return limits;
}

absl::Status SandboxBase::Allocate(v::Var* var, bool automatic_free) {
  if (!is_active()) {
    return absl::UnavailableError("Sandbox not active");
  }
  return var->Allocate(rpc_channel(), automatic_free);
}

absl::Status SandboxBase::Free(v::Var* var) {
  if (!is_active()) {
    return absl::UnavailableError("Sandbox not active");
  }
  return var->Free(rpc_channel());
}

absl::Status SandboxBase::SynchronizePtrBefore(v::Ptr* p) {
  if (!is_active()) {
    return absl::UnavailableError("Sandbox not active");
  }
  // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
  if (p->GetSyncType() == v::Ptr::kSyncNone) {
    return absl::OkStatus();
  }

  if (p->GetPointedVar()->GetRemote() == nullptr) {
    // Allocate the memory, and make it automatically free-able, upon this
    // object's (p->GetPointedVar()) end of life-time.
    SAPI_RETURN_IF_ERROR(Allocate(p->GetPointedVar(), /*automatic_free=*/true));
  }

  // Allocation occurs during both before/after synchronization modes. But the
  // memory is transferred to the sandboxee only if v::Ptr::kSyncBefore was
  // requested.
  // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
  if ((p->GetSyncType() & v::Ptr::kSyncBefore) == 0) {
    return absl::OkStatus();
  }

  VLOG(3) << "Synchronization (TO), ptr " << p << ", Type: " << p->GetSyncType()
          << " for var: " << p->GetPointedVar()->ToString();

  return p->GetPointedVar()->TransferToSandboxee(rpc_channel());
}

absl::Status SandboxBase::SynchronizePtrAfter(v::Ptr* p) const {
  if (!is_active()) {
    return absl::UnavailableError("Sandbox not active");
  }
  // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
  if ((p->GetSyncType() & v::Ptr::kSyncAfter) == 0) {
    return absl::OkStatus();
  }

  VLOG(3) << "Synchronization (FROM), ptr " << p
          << ", Type: " << p->GetSyncType()
          << " for var: " << p->GetPointedVar()->ToString();

  if (p->GetPointedVar()->GetRemote() == nullptr) {
    LOG(ERROR) << "Trying to synchronize a variable which is not allocated in "
               << "the sandboxee p=" << p->ToString();
    return absl::FailedPreconditionError(absl::StrCat(
        "Trying to synchronize a variable which is not allocated in the "
        "sandboxee p=",
        p->ToString()));
  }

  return p->GetPointedVar()->TransferFromSandboxee(rpc_channel());
}

absl::Status SandboxBase::Call(
    const std::string& func, v::Callable* ret,
    std::initializer_list<internal::PtrOrCallable> args) {
  if (!is_active()) {
    return absl::UnavailableError("Sandbox not active");
  }
  // Send data.
  FuncCall rfcall{};
  rfcall.argc = args.size();
  absl::SNPrintF(rfcall.func, ABSL_ARRAYSIZE(rfcall.func), "%s", func);

  VLOG(1) << "CALL ENTRY: '" << func << "' with " << args.size()
          << " argument(s)";

  // Copy all arguments into rfcall.
  for (int i = 0; i < args.size(); ++i) {
    const internal::PtrOrCallable& arg = args.begin()[i];
    if (arg.IsPtr()) {
      v::Ptr* parg = arg.ptr();
      rfcall.arg_size[i] = sizeof(void*);
      rfcall.arg_type[i] = v::Type::kPointer;
      if (parg == nullptr) {
        rfcall.args[i].arg_int = 0;
        VLOG(1) << "CALL ARG: (" << i << "): nullptr";
        continue;
      }
      if (v::Var* pvar = parg->GetPointedVar(); pvar != nullptr) {
        rfcall.aux_type[i] = pvar->GetType();
        rfcall.aux_size[i] = pvar->GetSize();
      }

      // Synchronize all pointers before the call if it's needed.
      SAPI_RETURN_IF_ERROR(SynchronizePtrBefore(parg));
      rfcall.args[i].arg_int = parg->GetRemoteValue();
      VLOG(1) << "CALL ARG: (" << i << "): " << parg->ToString();
      continue;
    }

    v::Callable* carg = arg.callable();
    rfcall.arg_size[i] = carg->GetSize();
    rfcall.arg_type[i] = carg->GetType();
    if (carg->GetType() == v::Type::kFloat) {
      memcpy(&rfcall.args[i].arg_float, carg->GetLocal(),
             std::min(sizeof(rfcall.args[0].arg_float), carg->GetSize()));
      // Make MSAN happy with long double.
      ABSL_ANNOTATE_MEMORY_IS_INITIALIZED(&rfcall.args[i].arg_float,
                                          sizeof(rfcall.args[0].arg_float));
    } else if (carg->GetSize() != 0) {
      memcpy(&rfcall.args[i].arg_int, carg->GetLocal(),
             std::min(sizeof(rfcall.args[0].arg_int), carg->GetSize()));
    }

    if (rfcall.arg_type[i] == v::Type::kFd) {
      // Cast is safe, since type is v::Type::kFd
      auto* fd = static_cast<v::Fd*>(carg);
      if (fd->GetRemoteFd() < 0) {
        SAPI_RETURN_IF_ERROR(TransferToSandboxee(fd));
      }
      rfcall.args[i].arg_int = fd->GetRemoteFd();
    }
    VLOG(1) << "CALL ARG: (" << i << "), Type: " << carg->GetTypeString()
            << ", Size: " << carg->GetSize() << ", Val: " << carg->ToString();
  }
  rfcall.ret_type = ret->GetType();
  rfcall.ret_size = ret->GetSize();

  // Call & receive data.
  FuncRet fret;
  SAPI_RETURN_IF_ERROR(
      rpc_channel()->Call(rfcall, comms::kMsgCall, &fret, rfcall.ret_type));

  if (fret.ret_type == v::Type::kFloat) {
    memcpy(ret->GetLocal(), &fret.float_val,
           std::min(ret->GetSize(), sizeof(fret.float_val)));
  } else if (ret->GetSize() != 0) {
    memcpy(ret->GetLocal(), &fret.int_val,
           std::min(ret->GetSize(), sizeof(fret.int_val)));
  }

  if (rfcall.ret_type == v::Type::kFd) {
    SAPI_RETURN_IF_ERROR(TransferFromSandboxee(ret));
  }

  // Synchronize all pointers after the call if it's needed.
  for (internal::PtrOrCallable arg : args) {
    if (arg.IsPtr() && arg.ptr() != nullptr) {
      SAPI_RETURN_IF_ERROR(SynchronizePtrAfter(arg.ptr()));
    }
  }

  VLOG(1) << "CALL EXIT: Type: " << ret->GetTypeString()
          << ", Size: " << ret->GetSize() << ", Val: " << ret->ToString();

  return absl::OkStatus();
}

absl::Status SandboxBase::Symbol(const char* symname, void** addr) {
  if (!is_active()) {
    return absl::UnavailableError("Sandbox not active");
  }
  return rpc_channel()->Symbol(symname, addr);
}

absl::Status SandboxBase::TransferToSandboxee(v::Var* var) {
  if (!is_active()) {
    return absl::UnavailableError("Sandbox not active");
  }
  return var->TransferToSandboxee(rpc_channel());
}

absl::Status SandboxBase::TransferFromSandboxee(v::Var* var) {
  if (!is_active()) {
    return absl::UnavailableError("Sandbox not active");
  }
  return var->TransferFromSandboxee(rpc_channel());
}

absl::StatusOr<std::unique_ptr<sapi::v::Array<const uint8_t>>>
SandboxBase::AllocateAndTransferToSandboxee(absl::Span<const uint8_t> buffer) {
  auto sapi_buffer = std::make_unique<sapi::v::Array<const uint8_t>>(
      buffer.data(), buffer.size());
  SAPI_RETURN_IF_ERROR(Allocate(sapi_buffer.get(), /*automatic_free=*/true));
  SAPI_RETURN_IF_ERROR(TransferToSandboxee(sapi_buffer.get()));
  return sapi_buffer;
}

absl::StatusOr<std::string> SandboxBase::GetCString(const v::RemotePtr& str,
                                                    size_t max_length) {
  if (!is_active()) {
    return absl::UnavailableError("Sandbox not active");
  }

  void* rptr = reinterpret_cast<void*>(str.GetRemoteValue());

  SAPI_ASSIGN_OR_RETURN(auto len, rpc_channel()->Strlen(rptr));
  if (len > max_length) {
    return absl::InvalidArgumentError(
        absl::StrCat("Target string too large: ", len, " > ", max_length));
  }
  std::string buffer(len, '\0');
  SAPI_ASSIGN_OR_RETURN(
      size_t ret,
      rpc_channel()->CopyFromSandbox(
          reinterpret_cast<uintptr_t>(rptr),
          absl::MakeSpan(reinterpret_cast<char*>(buffer.data()), len)));
  if (ret != len) {
    LOG(WARNING) << "partial read when reading c-string: CopyFromSandbox("
                 << "raddr: " << rptr << " size: " << len << ") transferred "
                 << ret << " bytes";
    return absl::UnavailableError("process_vm_readv succeeded partially");
  }

  return buffer;
}

}  // namespace sapi
