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

#ifndef CONTRIB_JSONNET_BASE_SANDBOX_H_
#define CONTRIB_JSONNET_BASE_SANDBOX_H_

#include <libgen.h>
#include <syscall.h>

#include <memory>
#include <string>
#include <utility>

#include "jsonnet_sapi.sapi.h"  // NOLINT(build/include)
#include "sandboxed_api/transaction.h"
#include "sandboxed_api/vars.h"

class JsonnetBaseSandbox : public JsonnetSandbox {
 public:
  explicit JsonnetBaseSandbox(std::string in_file, std::string out_file)
      : in_file_(std::move(in_file)), out_file_(std::move(out_file)) {}

  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    return sandbox2::PolicyBuilder()
        .AllowStaticStartup()
        .AllowOpen()
        .AllowRead()
        .AllowWrite()
        .AllowStat()
        .AllowSystemMalloc()
        .AllowExit()
        .AllowSyscalls({
            __NR_futex,
            __NR_close,
        })
        .AddDirectoryAt(dirname(&out_file_[0]), "/output", /*is_ro=*/false)
        .AddDirectoryAt(dirname(&in_file_[0]), "/input", true)
        .BuildOrDie();
  }

 private:
  std::string in_file_;
  std::string out_file_;
};

#endif  // CONTRIB_JSONNET_BASE_SANDBOX_H_
