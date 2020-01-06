// Copyright 2020 Google LLC. All Rights Reserved.
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

#include "sandboxed_api/sandbox2/util/strerror.h"

#include <string.h>  // For strerror_r

#include <cerrno>
#include <cstddef>

#include "absl/base/attributes.h"
#include "absl/strings/str_cat.h"

namespace sandbox2 {
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

std::string StrError(int errnum) {
  const int saved_errno = errno;
  char buf[100];
  const char* str = StrErrorR(strerror_r, errnum, buf, sizeof(buf));
  if (*str == '\0') {
    return absl::StrCat("Unknown error ", errnum);
  }
  errno = saved_errno;
  return str;
}

}  // namespace sandbox2
