// Copyright 2019 Google LLC
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

#include "sandboxed_api/util/strerror.h"

#include <string.h>  // For strerror_r

#include <cerrno>
#include <cstddef>

#include "absl/base/attributes.h"
#include "absl/strings/str_format.h"

namespace sapi {
namespace {

// Only one of these overloads will be used in any given build, as determined by
// the return type of strerror_r(): char* (for GNU), or int (for XSI). See 'man
// strerror_r' for more details.
ABSL_ATTRIBUTE_UNUSED const char* StrErrorR(char* (*strerror_r)(int, char*,
                                                                size_t),
                                            int errnum, char* buf,
                                            size_t buflen) {
  return strerror_r(errnum, buf, buflen);
}

// The XSI version (most portable).
ABSL_ATTRIBUTE_UNUSED const char* StrErrorR(int (*strerror_r)(int, char*,
                                                              size_t),
                                            int errnum, char* buf,
                                            size_t buflen) {
  if (strerror_r(errnum, buf, buflen)) {
    *buf = '\0';
  }
  return buf;
}

}  // namespace

const char* RawStrError(int errnum, char* buf, size_t buflen) {
  const int saved_errno = errno;
  const char* str = StrErrorR(strerror_r, errnum, buf, buflen);
  if (*str == '\0') {
    absl::SNPrintF(buf, buflen, "Unknown error %d", errnum);
    str = buf;
  }
  errno = saved_errno;
  return str;
}

std::string StrError(int errnum) {
  char buf[100];
  return RawStrError(errnum, buf, sizeof(buf));
}

}  // namespace sapi
