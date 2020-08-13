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

#ifndef SAPI_LODEPNG_SANDBOX_H_
#define SAPI_LODEPNG_SANDBOX_H_

#include <syscall.h>
#include <unistd.h>

#include "lodepng_sapi.sapi.h"

class SapiLodepngSandbox : public LodepngSandbox {
 public:
  SapiLodepngSandbox(const std::string &images_path)
      : images_path_(images_path) {}

  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder *) override {
    return sandbox2::PolicyBuilder()
        .AllowRead()
        .AllowWrite()
        .AllowOpen()
        .AllowSystemMalloc()
        .AllowExit()
        .AllowStat()
        .AddDirectoryAt(images_path_, "/output/", /*is_ro=*/false)
        .AllowSyscalls({
            __NR_futex,
            __NR_lseek,
            __NR_close,
        })
        .BuildOrDie();
  }

 private:
  std::string images_path_;
};

#endif  // SAPI_LODEPNG_SANDBOX_H_