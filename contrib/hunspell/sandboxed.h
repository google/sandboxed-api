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

#ifndef CONTRIB_HUNSPELL_SANDBOXED_H_
#define CONTRIB_HUNSPELL_SANDBOXED_H_

#include <libgen.h>
#include <syscall.h>

#include "sapi_hunspell.sapi.h"  // NOLINT(build/include)

class HunspellSapiSandbox : public HunspellSandbox {
 public:
  explicit HunspellSapiSandbox(std::string affix_file_name,
                               std::string dictionary_file_name)
      : affix_file_name_(std::move(affix_file_name)),
        dictionary_file_name_(std::move(dictionary_file_name)) {}

 private:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    return sandbox2::PolicyBuilder()
        .AllowStaticStartup()
        .AllowOpen()
        .AllowRead()
        .AllowWrite()
        .AllowGetPIDs()
        .AllowSystemMalloc()
        .AllowExit()
        .AllowSyscalls({
            __NR_clock_gettime,
            __NR_close,
        })
        .AddFile(affix_file_name_, /*is_ro=*/true)
        .AddFile(dictionary_file_name_, /*is_ro=*/true)
        .AllowRestartableSequencesWithProcFiles(
            sandbox2::PolicyBuilder::kAllowSlowFences)  // hangs without it
        .BuildOrDie();
  }

  std::string affix_file_name_;
  std::string dictionary_file_name_;
};

#endif  // CONTRIB_HUNSPELL_SANDBOXED_H_
