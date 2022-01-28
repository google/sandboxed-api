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

#ifndef SANDBOXED_API_UTIL_STRERROR_H_
#define SANDBOXED_API_UTIL_STRERROR_H_

#include <string>

namespace sapi {

// Returns a human-readable string describing the given POSIX error code. This
// is a portable and thread-safe alternative to strerror(). If the error code is
// not translatable, the string will be "Unknown error nnn". errno will not be
// modified by this call. This function is thread-safe.
std::string StrError(int errnum);

// Same as StrError but uses provided buffer (does not allocate memory).
// Might use and return a static immutable buffer instead of buf.
const char* RawStrError(int errnum, char* buf, size_t buflen);

}  // namespace sapi

#endif  // SANDBOXED_API_STRERROR_H_
