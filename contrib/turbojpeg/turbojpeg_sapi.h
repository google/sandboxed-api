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

#ifndef CONTRIB_TURBOJPEG_TURBOJPEG_SAPI_H_
#define CONTRIB_TURBOJPEG_TURBOJPEG_SAPI_H_

#include <syscall.h>

#include "sandboxed_api/util/fileops.h"
#include "turbojpeg_sapi.sapi.h"  // NOLINT(build/include)
class TurboJpegSapiSandbox : public turbojpeg_sapi::TurboJPEGSandbox {
 public:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    return sandbox2::PolicyBuilder()
        .AllowSystemMalloc()
        .AllowRead()
        .AllowStat()
        .AllowWrite()
        .AllowExit()
        .AllowSyscalls({
            __NR_futex,
            __NR_close,
            __NR_lseek,
            __NR_getpid,
            __NR_clock_gettime,
        })
        .AllowLlvmSanitizers()
        .BuildOrDie();
  }
};

#endif  // CONTRIB_TURBOJPEG_TURBOJPEG_SAPI_H_
