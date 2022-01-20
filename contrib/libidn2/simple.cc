// Copyright 2021 Google LLC
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

#include <gflags/gflags.h>
#include <syscall.h>

#include <fstream>
#include <iostream>
#include <cstdlib>

#include <glog/logging.h>
#include <idn2.h>
#include "libidn2_sapi.sapi.h"  // NOLINT(build/include)
#include "sandboxed_api/util/fileops.h"

#include "simple.hh"

class Idn2SapiSandbox : public IDN2Sandbox {
 public:
  Idn2SapiSandbox()
      : IDN2Sandbox() {}

  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    return sandbox2::PolicyBuilder()
        .AllowDynamicStartup()
        .AllowSystemMalloc()
        .AllowRead()
        .AllowOpen()
        .AllowStat()
        .AllowWrite()
        .AllowExit()
        .AllowSyscalls({
            __NR_futex,
            __NR_close,
            __NR_recvmsg,
            __NR_lseek,
            __NR_getpid,
            __NR_sysinfo,
            __NR_prlimit64,
        })
        .BuildOrDie();
  }
};

class IDN2Lib {
  Idn2SapiSandbox *sandbox;
  IDN2Api *api;
  absl::StatusOr<std::string> sapi_generic(
      const char *data,
      absl::StatusOr<int>(IDN2Api::*cb)(sapi::v::Ptr *input, sapi::v::Ptr *output, int flags));
  absl::StatusOr<std::string> process_errors(absl::StatusOr<int> status, sapi::v::GenericPtr &ptr);
 public:
  IDN2Lib &operator =(IDN2Lib &other) = delete;
  IDN2Lib &operator =(const IDN2Lib &other) = delete;
  IDN2Lib(IDN2Lib &other) = delete;
  IDN2Lib(const IDN2Lib &other) = delete;
  IDN2Lib(Idn2SapiSandbox *sandbox, IDN2Api *api) :
    sandbox(sandbox ? sandbox : (abort(), nullptr)),
    api(api ? api : (abort(), nullptr)) {}
  absl::StatusOr<std::string> idn2_register_u8(const char *ulabel, const char *alabel);
  absl::StatusOr<std::string> idn2_lookup_u8(const char *data);
  absl::StatusOr<std::string> idn2_to_ascii_8z(const char *ulabel);
  absl::StatusOr<std::string> idn2_to_unicode_8z8z(const char *ulabel);
};

absl::StatusOr<std::string> IDN2Lib::process_errors(absl::StatusOr<int> untrusted_res, sapi::v::GenericPtr &ptr) {
  if (!untrusted_res.ok()) {
    return untrusted_res.status();
  }
  int res = untrusted_res.value();
  if (res < 0) {
    if (res == IDN2_MALLOC)
      return absl::ResourceExhaustedError("malloc() failed in libidn2");
    if (res > -10000)
      return absl::InvalidArgumentError(idn2_strerror(res));
    return absl::InvalidArgumentError("Unexpected error");
  }
  ::sapi::v::RemotePtr p(reinterpret_cast<void *>(ptr.GetValue()));
  auto maybe_untrusted_name = sandbox->GetCString(p, 256);
  SAPI_RETURN_IF_ERROR(sandbox->Free(&p));
  if (!maybe_untrusted_name.ok()) {
    return maybe_untrusted_name.status();
  }
  // FIXME: sanitize the result
  return *maybe_untrusted_name;
}

absl::StatusOr<std::string> IDN2Lib::idn2_register_u8(const char *ulabel, const char *alabel) {
  ::sapi::v::ConstCStr alabel_ptr(reinterpret_cast<const char *>(alabel)),
                       ulabel_ptr(reinterpret_cast<const char *>(ulabel));
  ::sapi::v::GenericPtr ptr;

  const auto untrusted_res =
    api->idn2_register_u8(ulabel_ptr.PtrBefore(), alabel_ptr.PtrBefore(),
                         ptr.PtrAfter(),
                         IDN2_NFC_INPUT | IDN2_NONTRANSITIONAL);
  return this->process_errors(untrusted_res, ptr);
}

absl::StatusOr<std::string> IDN2Lib::sapi_generic(
    const char *data,
    absl::StatusOr<int>(IDN2Api::*cb)(sapi::v::Ptr *input, sapi::v::Ptr *output, int flags)) {
  ::sapi::v::ConstCStr src(data);
  ::sapi::v::GenericPtr ptr;

  absl::StatusOr<int> untrusted_res =
    ((api)->*(cb))(src.PtrBefore(), ptr.PtrAfter(), IDN2_NFC_INPUT | IDN2_NONTRANSITIONAL);
  return this->process_errors(untrusted_res, ptr);
}

absl::StatusOr<std::string> IDN2Lib::idn2_to_unicode_8z8z(const char *data) {
  return IDN2Lib::sapi_generic(data, &IDN2Api::idn2_to_unicode_8z8z);
}

absl::StatusOr<std::string> IDN2Lib::idn2_to_ascii_8z(const char *data) {
  return IDN2Lib::sapi_generic(data, &IDN2Api::idn2_to_ascii_8z);
}

absl::StatusOr<std::string> IDN2Lib::idn2_lookup_u8(const char *data) {
  return IDN2Lib::sapi_generic(data, &IDN2Api::idn2_lookup_u8);
}

int main(int argc, char **argv) {
  Idn2SapiSandbox sandbox{};

  auto status = sandbox.Init();
  if (!status.ok()) {
    std::cerr << "Failed to initialize sandbox: " << status << std::endl;
    return 1;
  }
  IDN2Api api(&sandbox);
  IDN2Lib lib(&sandbox, &api);
  for (int i = 1; i < argc; ++i) {
    const auto this_status{lib.idn2_lookup_u8(argv[i])};
    if (!this_status.ok()) {
      std::cerr << "Failed to process argument " << argv[i] << ": " << this_status.status() << std::endl;
      return 1;
    }
    std::cout << this_status.value() << std::endl;
  }
}
