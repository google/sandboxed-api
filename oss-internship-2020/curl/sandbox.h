// Copyright 2020 Google LLC
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
      sandbox2::PolicyBuilder* policy_builder) override {
    // Return a new policy
    return (*policy_builder)
        .AllowDynamicStartup()
        .AllowExit()
        .AllowFork()
        .AllowFutexOp(FUTEX_WAKE_PRIVATE)
        .AllowMmap()
        .AllowOpen()
        .AllowRead()
        .AllowSafeFcntl()
        .AllowWrite()
        .AllowSyscalls({
            __NR_accept,
            __NR_access,
            __NR_bind,
            __NR_connect,
            __NR_getpeername,
            __NR_getsockname,
            __NR_getsockopt,
            __NR_ioctl,
            __NR_listen,
            __NR_madvise,
            __NR_poll,
            __NR_recvfrom,
            __NR_recvmsg,
            __NR_sendmmsg,
            __NR_sendto,
            __NR_setsockopt,
            __NR_socket,
        })
        .AllowUnrestrictedNetworking()
        .AddDirectory("/lib")
        .BuildOrDie();
  }
};

#endif  // SANDBOX_H_
