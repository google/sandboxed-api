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

#ifndef CONTRIB_LIBZIP_SANDBOXED_H_
#define CONTRIB_LIBZIP_SANDBOXED_H_

#include <libgen.h>
#include <syscall.h>

#include <memory>

// Note: This header is required because to work around an issue with the
//       libclang based header generator, which doesn't catch the types
//       defined by zip (for example zip_uint32_t).
#include <zipconf.h>  // NOLINT(build/include_order)

#include "sapi_zip.sapi.h"  // NOLINT(build/include)

class ZipSapiSandbox : public ZipSandbox {
 public:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    return sandbox2::PolicyBuilder()
        .AllowDynamicStartup()
        .AllowRead()
        .AllowWrite()
        .AllowSystemMalloc()
        .AllowGetPIDs()
        .AllowExit()
        .AllowSafeFcntl()
        .AllowSyscalls({
            __NR_dup,
            __NR_recvmsg,
            __NR_ftruncate,
        })
        .BlockSyscallWithErrno(__NR_openat, ENOENT)
        .BuildOrDie();
  }
};

#endif  // CONTRIB_LIBZIP_SANDBOXED_H_
