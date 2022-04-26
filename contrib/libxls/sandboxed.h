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

#ifndef CONTRIB_LIBXLS_SANDBOXED_H_
#define CONTRIB_LIBXLS_SANDBOXED_H_

#include <libgen.h>
#include <syscall.h>

#include <memory>

#include "sapi_libxls.sapi.h"  // NOLINT(build/include)

class LibxlsSapiSandbox : public LibxlsSandbox {
 public:
  explicit LibxlsSapiSandbox(std::string filename)
      : filename_(std::move(filename)) {}

  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    return sandbox2::PolicyBuilder()
        .AllowDynamicStartup()
        .AllowOpen()
        .AllowRead()
        .AllowWrite()
        .AllowSystemMalloc()
        .AllowExit()
        .AllowSyscall(__NR_recvmsg)
        .AddFile(filename_)
        .BuildOrDie();
  }

 private:
  std::string filename_;
};

#endif  // CONTRIB_LIBXLS_SANDBOXED_H_
