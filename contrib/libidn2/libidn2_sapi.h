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

#ifndef CONTRIB_LIBIDN2_LIBIDN2_SAPI_H_
#define CONTRIB_LIBIDN2_LIBIDN2_SAPI_H_

#include <idn2.h>
#include <syscall.h>

#include <cstdlib>

#include "libidn2_sapi.sapi.h"  // NOLINT(build/include)
#include "absl/log/die_if_null.h"
#include "sandboxed_api/util/fileops.h"

class Idn2SapiSandbox : public IDN2Sandbox {
 public:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    return sandbox2::PolicyBuilder()
        .AllowSystemMalloc()
        .AllowRead()
        .AllowStat()
        .AllowWrite()
        .AllowExit()
        .AllowGetPIDs()
        .AllowSyscalls({
            __NR_futex,
            __NR_close,
            __NR_lseek,
        })
        .BlockSyscallWithErrno(__NR_openat, ENOENT)
        .BuildOrDie();
  }
};

class IDN2Lib {
 public:
  explicit IDN2Lib(Idn2SapiSandbox* sandbox)
      : sandbox_(ABSL_DIE_IF_NULL(sandbox)), api_(sandbox_) {}
  absl::StatusOr<std::string> idn2_register_u8(const char* ulabel,
                                               const char* alabel);
  absl::StatusOr<std::string> idn2_lookup_u8(const char* data);
  absl::StatusOr<std::string> idn2_to_ascii_8z(const char* ulabel);
  absl::StatusOr<std::string> idn2_to_unicode_8z8z(const char* ulabel);

 private:
  absl::StatusOr<std::string> SapiGeneric(
      const char* data,
      absl::StatusOr<int> (IDN2Api::*cb)(sapi::v::Ptr* input,
                                         sapi::v::Ptr* output, int flags));
  absl::StatusOr<std::string> ProcessErrors(const absl::StatusOr<int>& status,
                                            sapi::v::GenericPtr& ptr);
  Idn2SapiSandbox* sandbox_;
  IDN2Api api_;
};

#endif  // CONTRIB_LIBIDN2_LIBIDN2_SAPI_H_
