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

#include "libidn2_sapi.h"  // NOLINT(build/include)

#include <cstdlib>
#include <fstream>
#include <iostream>

#include "absl/log/log.h"
#include "sandboxed_api/util/fileops.h"

static constexpr std::size_t kMaxDomainNameLength = 256;
static constexpr int kMinPossibleKnownError = -10000;

absl::StatusOr<std::string> IDN2Lib::ProcessErrors(
    const absl::StatusOr<int>& untrusted_res, sapi::v::GenericPtr& ptr) {
  SAPI_RETURN_IF_ERROR(untrusted_res.status());
  int res = untrusted_res.value();
  if (res < 0) {
    if (res == IDN2_MALLOC) {
      return absl::ResourceExhaustedError("malloc() failed in libidn2");
    }
    if (res > kMinPossibleKnownError) {
      return absl::InvalidArgumentError(idn2_strerror(res));
    }
    return absl::InvalidArgumentError("Unexpected error");
  }
  sapi::v::RemotePtr p(reinterpret_cast<void*>(ptr.GetValue()));
  auto maybe_untrusted_name = sandbox_->GetCString(p, kMaxDomainNameLength);
  SAPI_RETURN_IF_ERROR(sandbox_->Free(&p));
  if (!maybe_untrusted_name.ok()) {
    return maybe_untrusted_name.status();
  }
  // FIXME: sanitize the result by checking that the return value is
  // valid ASCII (for a-labels) or UTF-8 (for u-labels) and doesn't
  // contain potentially malicious characters.
  return *maybe_untrusted_name;
}

absl::StatusOr<std::string> IDN2Lib::idn2_register_u8(const char* ulabel,
                                                      const char* alabel) {
  std::optional<sapi::v::ConstCStr> alabel_ptr;
  std::optional<sapi::v::ConstCStr> ulabel_ptr;
  if (ulabel) {
    ulabel_ptr.emplace(ulabel);
  }
  if (alabel) {
    alabel_ptr.emplace(alabel);
  }
  sapi::v::GenericPtr ptr;
  sapi::v::NullPtr null_ptr;
  const auto untrusted_res = api_.idn2_register_u8(
      ulabel ? ulabel_ptr->PtrBefore() : &null_ptr,
      alabel ? alabel_ptr->PtrBefore() : &null_ptr, ptr.PtrAfter(),
      IDN2_NFC_INPUT | IDN2_NONTRANSITIONAL);
  return this->ProcessErrors(untrusted_res, ptr);
}

absl::StatusOr<std::string> IDN2Lib::SapiGeneric(
    const char* data,
    absl::StatusOr<int> (IDN2Api::*cb)(sapi::v::Ptr* input,
                                       sapi::v::Ptr* output, int flags)) {
  sapi::v::ConstCStr src(data);
  sapi::v::GenericPtr ptr;

  absl::StatusOr<int> untrusted_res = ((api_).*(cb))(
      src.PtrBefore(), ptr.PtrAfter(), IDN2_NFC_INPUT | IDN2_NONTRANSITIONAL);
  return this->ProcessErrors(untrusted_res, ptr);
}

absl::StatusOr<std::string> IDN2Lib::idn2_to_unicode_8z8z(const char* data) {
  return IDN2Lib::SapiGeneric(data, &IDN2Api::idn2_to_unicode_8z8z);
}

absl::StatusOr<std::string> IDN2Lib::idn2_to_ascii_8z(const char* data) {
  return IDN2Lib::SapiGeneric(data, &IDN2Api::idn2_to_ascii_8z);
}

absl::StatusOr<std::string> IDN2Lib::idn2_lookup_u8(const char* data) {
  return IDN2Lib::SapiGeneric(data, &IDN2Api::idn2_lookup_u8);
}
