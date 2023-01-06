// Copyright 2020 Google LLC
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

#ifndef SANDBOX_H_
#define SANDBOX_H_

#include <linux/futex.h>
#include <sys/mman.h>  // For mmap arguments
#include <syscall.h>

#include <cstdlib>

#include "curl_sapi.sapi.h"  // NOLINT(build/include)
#include "sandboxed_api/sandbox2/util/bpf_helper.h"

namespace curl {

class CurlSapiSandbox : public curl::CurlSandbox {
 protected:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    // Return a new policy
    return sandbox2::PolicyBuilder()
        .AllowDynamicStartup()
        .AllowExit()
        .AllowFork()
        .AllowFutexOp(FUTEX_WAIT_PRIVATE)
        .AllowFutexOp(FUTEX_WAKE_PRIVATE)
        .AllowFutexOp(FUTEX_REQUEUE_PRIVATE)
        .AllowMmap()
        .AllowOpen()
        .AllowSafeFcntl()
        .AllowWrite()
        .AllowAccess()
        .AllowSyscall(__NR_accept)
        .AllowSyscall(__NR_bind)
        .AllowSyscall(__NR_connect)
        .AllowSyscall(__NR_getpeername)
        .AllowSyscall(__NR_getsockname)
        .AllowSyscall(__NR_getsockopt)
        .AllowSyscall(__NR_ioctl)
        .AllowSyscall(__NR_listen)
        .AllowSyscall(__NR_madvise)
        .AllowPoll()
        .AllowSyscall(__NR_recvfrom)
        .AllowSyscall(__NR_recvmsg)
        .AllowSyscall(__NR_rt_sigaction)
        .AllowSyscall(__NR_sendmmsg)
        .AllowSyscall(__NR_sendto)
        .AllowSyscall(__NR_setsockopt)
        .AllowSyscall(__NR_socket)
        .AllowSyscall(__NR_sysinfo)
        .AddDirectory("/lib")
        .AllowUnrestrictedNetworking()
        .BuildOrDie();
  }
};

}  // namespace curl

#endif  // SANDBOX_H_
