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

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "sandboxed_api/file_toc.h"
#include "absl/base/dynamic_annotations.h"
#include "absl/base/macros.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/embed_file.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/runfiles.h"
#include "sandboxed_api/util/status_macros.h"
#include "sandboxed_api/var_abstract.h"
#include "sandboxed_api/var_array.h"
#include "sandboxed_api/var_int.h"
#include "sandboxed_api/var_ptr.h"
#include "sandboxed_api/var_reg.h"
#include "sandboxed_api/var_type.h"

namespace sapi {

Sandbox::Sandbox(const FileToc* embed_lib_toc) {
  owned_fork_client_context_ =
      std::make_unique<ForkClientContext>(embed_lib_toc);
  fork_client_context_ = owned_fork_client_context_.get();
}

Sandbox::Sandbox(std::nullptr_t)
    : Sandbox(static_cast<const FileToc*>(nullptr)) {}

Sandbox::~Sandbox() {
  Terminate();
  // The forkserver will die automatically when the executor goes out of scope
  // and closes the comms object.
}

void Sandbox::SetForkClientContext(ForkClientContext* fork_client_context) {
  fork_client_context_ = fork_client_context;
  owned_fork_client_context_.reset();
}

// A generic policy which should work with majority of typical libraries, which
// are single-threaded and require ~30 basic syscalls.
//
// IMPORTANT: This policy must be safe to use with
// `Allow(sandbox2::UnrestrictedNetworking())`.
void InitDefaultPolicyBuilder(sandbox2::PolicyBuilder* builder) {
  (*builder)
      .AllowRead()
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
  builder->AllowSyscall(__NR_arch_prctl);
#endif

  if constexpr (sanitizers::IsAny()) {
    LOG(WARNING) << "Allowing additional calls to support the LLVM "
                 << "(ASAN/MSAN/TSAN) sanitizer";
    builder->AllowLlvmSanitizers();
  }
  builder->AddFile("/etc/localtime")
      .AddTmpfs("/tmp", 1ULL << 30 /* 1GiB tmpfs (max size */);
}

void Sandbox::Terminate(bool attempt_graceful_exit) {
  if (!is_active()) {
    return;
  }

  absl::StatusOr<sandbox2::Result> result;
  if (attempt_graceful_exit) {
    if (absl::Status requested_exit = rpc_channel_->Exit();
        !requested_exit.ok()) {
      LOG(WARNING)
          << "rpc_channel->Exit() failed, calling AwaitResultWithTimeout(1) "
          << requested_exit;
    }
    result = s2_->AwaitResultWithTimeout(absl::Seconds(1));
    if (!result.ok()) {
      LOG(WARNING) << "s2_->AwaitResultWithTimeout failed, status: "
                   << result.status() << " Killing PID: " << pid();
    }
  }

  if (!attempt_graceful_exit || !result.ok()) {
    s2_->Kill();
    result = s2_->AwaitResult();
  }

  if (result->final_status() == sandbox2::Result::OK &&
      result->reason_code() == 0) {
    VLOG(2) << "Sandbox2 finished with: " << result->ToString();
  } else {
    LOG(WARNING) << "Sandbox2 finished with: " << result->ToString();
  }
}

static std::string PathToSAPILib(const std::string& lib_path) {
  return file::IsAbsolutePath(lib_path) ? lib_path
                                        : GetDataDependencyFilePath(lib_path);
}

absl::Status Sandbox::Init(bool use_unotify_monitor) {
  // It's already initialized
  if (is_active()) {
    return absl::OkStatus();
  }

  sandbox2::ForkClient* fork_client;
  {
    absl::MutexLock lock(&fork_client_context_->mu_);
    // Initialize the forkserver if it is not already running.
    if (!fork_client_context_->client_) {
      // If FileToc was specified, it will be used over any paths to the SAPI
      // library.
      std::string lib_path;
      int embed_lib_fd = -1;
      const FileToc* embed_lib_toc = fork_client_context_->embed_lib_toc_;
      if (embed_lib_toc) {
        embed_lib_fd = EmbedFile::instance()->GetDupFdForFileToc(embed_lib_toc);
        if (embed_lib_fd == -1) {
          PLOG(ERROR) << "Cannot create executable FD for TOC:'"
                      << embed_lib_toc->name << "'";
          return absl::UnavailableError("Could not create executable FD");
        }
        lib_path = embed_lib_toc->name;
      } else {
        lib_path = PathToSAPILib(GetLibPath());
        if (lib_path.empty()) {
          LOG(ERROR) << "SAPI library path is empty";
          return absl::FailedPreconditionError("No SAPI library path given");
        }
      }
      std::vector<std::string> args = {lib_path};
      // Additional arguments, if needed.
      GetArgs(&args);
      std::vector<std::string> envs{};
      // Additional envvars, if needed.
      GetEnvs(&envs);

      fork_client_context_->executor_ =
          (embed_lib_fd >= 0)
              ? std::make_unique<sandbox2::Executor>(embed_lib_fd, args, envs)
              : std::make_unique<sandbox2::Executor>(lib_path, args, envs);

      fork_client_context_->client_ =
          fork_client_context_->executor_->StartForkServer();

      if (!fork_client_context_->client_) {
        LOG(ERROR) << "Could not start forkserver";
        return absl::UnavailableError("Could not start the forkserver");
      }
    }
    fork_client = fork_client_context_->client_.get();
  }

    sandbox2::PolicyBuilder policy_builder;
    InitDefaultPolicyBuilder(&policy_builder);
  if (use_unotify_monitor) {
    policy_builder.CollectStacktracesOnSignal(false);
  }
  auto s2p = ModifyPolicy(&policy_builder);

  // Spawn new process from the forkserver.
  auto executor = std::make_unique<sandbox2::Executor>(fork_client);

  executor
      // The client.cc code is capable of enabling sandboxing on its own.
      ->set_enable_sandbox_before_exec(false)
      // By default, set cwd to "/", can be changed in ModifyExecutor().
      .set_cwd("/")
      .limits()
      // Disable time limits.
      ->set_walltime_limit(absl::ZeroDuration())
      .set_rlimit_cpu(RLIM64_INFINITY);

  // Modify the executor, e.g. by setting custom limits and IPC.
  ModifyExecutor(executor.get());

  s2_ = std::make_unique<sandbox2::Sandbox2>(std::move(executor),
                                             std::move(s2p), CreateNotifier());
  if (use_unotify_monitor) {
    SAPI_RETURN_IF_ERROR(s2_->EnableUnotifyMonitor());
  }
  s2_awaited_ = false;
  auto res = s2_->RunAsync();

  comms_ = s2_->comms();
  pid_ = s2_->pid();

  rpc_channel_ = std::make_unique<RPCChannel>(comms_);

  if (!res) {
    // Allow recovering from a bad fork client state.
    {
      absl::MutexLock lock(&fork_client_context_->mu_);
      fork_client_context_->client_.reset();
    }
    Terminate();
    return absl::UnavailableError("Could not start the sandbox");
  }
  return absl::OkStatus();
}

bool Sandbox::is_active() const { return s2_ && !s2_->IsTerminated(); }

absl::Status Sandbox::Allocate(v::Var* var, bool automatic_free) {
  if (!is_active()) {
    return absl::UnavailableError("Sandbox not active");
  }
  return var->Allocate(rpc_channel(), automatic_free);
}

absl::Status Sandbox::Free(v::Var* var) {
  if (!is_active()) {
    return absl::UnavailableError("Sandbox not active");
  }
  return var->Free(rpc_channel());
}

absl::Status Sandbox::SynchronizePtrBefore(v::Callable* ptr) {
  if (!is_active()) {
    return absl::UnavailableError("Sandbox not active");
  }
  if (ptr->GetType() != v::Type::kPointer) {
    return absl::OkStatus();
  }
  // Cast is safe, since type is v::Type::kPointer
  auto* p = static_cast<v::Ptr*>(ptr);
  // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
  if (p->GetSyncType() == v::Pointable::kSyncNone) {
    return absl::OkStatus();
  }

  if (p->GetPointedVar()->GetRemote() == nullptr) {
    // Allocate the memory, and make it automatically free-able, upon this
    // object's (p->GetPointedVar()) end of life-time.
    SAPI_RETURN_IF_ERROR(Allocate(p->GetPointedVar(), /*automatic_free=*/true));
  }

  // Allocation occurs during both before/after synchronization modes. But the
  // memory is transferred to the sandboxee only if v::Pointable::kSyncBefore
  // was requested.
  // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
  if ((p->GetSyncType() & v::Pointable::kSyncBefore) == 0) {
    return absl::OkStatus();
  }

  VLOG(3) << "Synchronization (TO), ptr " << p << ", Type: " << p->GetSyncType()
          << " for var: " << p->GetPointedVar()->ToString();

  return p->GetPointedVar()->TransferToSandboxee(rpc_channel(), pid());
}

absl::Status Sandbox::SynchronizePtrAfter(v::Callable* ptr) const {
  if (!is_active()) {
    return absl::UnavailableError("Sandbox not active");
  }
  if (ptr->GetType() != v::Type::kPointer) {
    return absl::OkStatus();
  }
  v::Ptr* p = reinterpret_cast<v::Ptr*>(ptr);
  // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
  if ((p->GetSyncType() & v::Pointable::kSyncAfter) == 0) {
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

  return p->GetPointedVar()->TransferFromSandboxee(rpc_channel(), pid());
}

absl::Status Sandbox::Call(const std::string& func, v::Callable* ret,
                           std::initializer_list<v::Callable*> args) {
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
  int i = 0;
  for (auto* arg : args) {
    if (arg == nullptr) {
      rfcall.arg_type[i] = v::Type::kPointer;
      rfcall.arg_size[i] = sizeof(void*);
      rfcall.args[i].arg_int = 0;
      VLOG(1) << "CALL ARG: (" << i << "): nullptr";
      ++i;
      continue;
    }
    rfcall.arg_size[i] = arg->GetSize();
    rfcall.arg_type[i] = arg->GetType();

    // For pointers, set the auxiliary type and size.
    if (rfcall.arg_type[i] == v::Type::kPointer) {
      // Cast is safe, since type is v::Type::kPointer
      auto* p = static_cast<v::Ptr*>(arg);
      rfcall.aux_type[i] = p->GetPointedVar()->GetType();
      rfcall.aux_size[i] = p->GetPointedVar()->GetSize();
    }

    // Synchronize all pointers before the call if it's needed.
    SAPI_RETURN_IF_ERROR(SynchronizePtrBefore(arg));

    if (arg->GetType() == v::Type::kFloat) {
      arg->GetDataFromPtr(&rfcall.args[i].arg_float,
                          sizeof(rfcall.args[0].arg_float));
      // Make MSAN happy with long double.
      ABSL_ANNOTATE_MEMORY_IS_INITIALIZED(&rfcall.args[i].arg_float,
                                          sizeof(rfcall.args[0].arg_float));
    } else {
      arg->GetDataFromPtr(&rfcall.args[i].arg_int,
                          sizeof(rfcall.args[0].arg_int));
    }

    if (rfcall.arg_type[i] == v::Type::kFd) {
      // Cast is safe, since type is v::Type::kFd
      auto* fd = static_cast<v::Fd*>(arg);
      if (fd->GetRemoteFd() < 0) {
        SAPI_RETURN_IF_ERROR(TransferToSandboxee(fd));
      }
      rfcall.args[i].arg_int = fd->GetRemoteFd();
    }
    VLOG(1) << "CALL ARG: (" << i << "), Type: " << arg->GetTypeString()
            << ", Size: " << arg->GetSize() << ", Val: " << arg->ToString();
    ++i;
  }
  rfcall.ret_type = ret->GetType();
  rfcall.ret_size = ret->GetSize();

  // Call & receive data.
  FuncRet fret;
  SAPI_RETURN_IF_ERROR(
      rpc_channel()->Call(rfcall, comms::kMsgCall, &fret, rfcall.ret_type));

  if (fret.ret_type == v::Type::kFloat) {
    ret->SetDataFromPtr(&fret.float_val, sizeof(fret.float_val));
  } else {
    ret->SetDataFromPtr(&fret.int_val, sizeof(fret.int_val));
  }

  if (fret.ret_type == v::Type::kFd) {
    SAPI_RETURN_IF_ERROR(TransferFromSandboxee(reinterpret_cast<v::Fd*>(ret)));
  }

  // Synchronize all pointers after the call if it's needed.
  for (auto* arg : args) {
    if (arg != nullptr) {
      SAPI_RETURN_IF_ERROR(SynchronizePtrAfter(arg));
    }
  }

  VLOG(1) << "CALL EXIT: Type: " << ret->GetTypeString()
          << ", Size: " << ret->GetSize() << ", Val: " << ret->ToString();

  return absl::OkStatus();
}

absl::Status Sandbox::Symbol(const char* symname, void** addr) {
  if (!is_active()) {
    return absl::UnavailableError("Sandbox not active");
  }
  return rpc_channel_->Symbol(symname, addr);
}

absl::Status Sandbox::TransferToSandboxee(v::Var* var) {
  if (!is_active()) {
    return absl::UnavailableError("Sandbox not active");
  }
  return var->TransferToSandboxee(rpc_channel(), pid());
}

absl::Status Sandbox::TransferFromSandboxee(v::Var* var) {
  if (!is_active()) {
    return absl::UnavailableError("Sandbox not active");
  }
  return var->TransferFromSandboxee(rpc_channel(), pid());
}

absl::StatusOr<std::unique_ptr<sapi::v::Array<const uint8_t>>>
Sandbox::AllocateAndTransferToSandboxee(absl::Span<const uint8_t> buffer) {
  auto sapi_buffer = std::make_unique<sapi::v::Array<const uint8_t>>(
      buffer.data(), buffer.size());
  SAPI_RETURN_IF_ERROR(Allocate(sapi_buffer.get(), /*automatic_free=*/true));
  SAPI_RETURN_IF_ERROR(TransferToSandboxee(sapi_buffer.get()));
  return sapi_buffer;
}

absl::StatusOr<std::string> Sandbox::GetCString(const v::RemotePtr& str,
                                                size_t max_length) {
  if (!is_active()) {
    return absl::UnavailableError("Sandbox not active");
  }

  SAPI_ASSIGN_OR_RETURN(auto len, rpc_channel()->Strlen(str.GetValue()));
  if (len > max_length) {
    return absl::InvalidArgumentError(
        absl::StrCat("Target string too large: ", len, " > ", max_length));
  }
  std::string buffer(len, '\0');
  SAPI_ASSIGN_OR_RETURN(
      size_t ret,
      sandbox2::util::ReadBytesFromPidInto(
          pid_, reinterpret_cast<uintptr_t>(str.GetValue()),
          absl::MakeSpan(reinterpret_cast<char*>(buffer.data()), len)));
  if (ret != len) {
    LOG(WARNING) << "partial read when reading c-string: process_vm_readv(pid: "
                 << pid_ << " raddr: " << str.GetValue() << " size: " << len
                 << ") transferred " << ret << " bytes";
    return absl::UnavailableError("process_vm_readv succeeded partially");
  }

  return buffer;
}

const sandbox2::Result& Sandbox::AwaitResult() {
  if (s2_ && !s2_awaited_) {
    result_ = s2_->AwaitResult();
    s2_awaited_ = true;
  }
  return result_;
}

absl::Status Sandbox::SetWallTimeLimit(absl::Duration limit) const {
  if (!is_active()) {
    return absl::UnavailableError("Sandbox not active");
  }
  s2_->set_walltime_limit(limit);
  return absl::OkStatus();
}

void Sandbox::Exit() const {
  if (!is_active()) {
    return;
  }
  s2_->set_walltime_limit(absl::Seconds(1));
  if (!rpc_channel_->Exit().ok()) {
    LOG(WARNING) << "rpc_channel->Exit() failed, killing PID: " << pid();
    s2_->Kill();
  }
}

std::unique_ptr<sandbox2::Policy> Sandbox::ModifyPolicy(
    sandbox2::PolicyBuilder* builder) {
  return builder->BuildOrDie();
}

}  // namespace sapi
