// Copyright 2022 Google LLC
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

#ifndef CONTRIB_ZOPFLI_SANDBOXED_
#define CONTRIB_ZOPFLI_SANDBOXED_

#include <libgen.h>
#include <syscall.h>

#include <cerrno>
#include <memory>

#include "sapi_zopfli.sapi.h"  // NOLINT(build/include)

class ZopfliSapiSandbox : public ZopfliSandbox {
 public:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder *) override {
    return sandbox2::PolicyBuilder()
        .AllowDynamicStartup()
        .AllowWrite()
        .AllowExit()
        .AllowMmap()
        .AllowSystemMalloc()
        .AllowSyscalls({
            __NR_recvmsg,
            __NR_sysinfo,
        })
#ifdef __NR_open
        .BlockSyscallWithErrno(__NR_open, ENOENT)
#endif
#ifdef __NR_openat
        .BlockSyscallWithErrno(__NR_openat, ENOENT)
#endif
        .BuildOrDie();
  }
};

#endif  // CONTRIB_ZOPFLI_SANDBOXED_
