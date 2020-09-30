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

#include <linux/futex.h>
#include <syscall.h>

#include <optional>
#include <utility>

#include "sandboxed_api/util/flag.h"
#include "tiff_sapi.sapi.h"

ABSL_DECLARE_FLAG(string, sandbox2_danger_danger_permit_all);
ABSL_DECLARE_FLAG(string, sandbox2_danger_danger_permit_all_and_log);

namespace {

class TiffSapiSandbox : public TiffSandbox {
 public:
  TiffSapiSandbox(std::optional<std::string> file = std::nullopt,
                  std::optional<std::string> dir = std::nullopt)
      : file_(std::move(file)), dir_(std::move(dir)) {}

 private:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    auto builder = std::make_unique<sandbox2::PolicyBuilder>();
    (*builder)
        .AllowRead()
        .AllowStaticStartup()
        .AllowWrite()
        .AllowOpen()
        .AllowExit()
        .AllowStat()
        .AllowMmap()
        .AllowSystemMalloc()
        .AllowSyscalls({
            __NR_futex,
            __NR_close,
            __NR_lseek,
            __NR_gettid,
            __NR_sysinfo,
            __NR_munmap,
        });

    if (file_) {
      builder->AddFile(file_.value(), /*is_ro=*/false);
    }

    if (dir_) {
      builder->AddDirectory(dir_.value(), /*is_ro=*/false);
    }

    return builder.get()->BuildOrDie();
  }

  std::optional<std::string> file_;
  std::optional<std::string> dir_;
};

}  // namespace
