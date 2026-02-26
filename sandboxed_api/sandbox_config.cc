// Copyright 2026 Google LLC
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

#include "sandboxed_api/sandbox_config.h"

#include <syscall.h>

#include "absl/log/log.h"
#include "absl/time/time.h"
#include "sandboxed_api/sandbox2/limits.h"
#include "sandboxed_api/sandbox2/policy.h"

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

}  // namespace sapi
