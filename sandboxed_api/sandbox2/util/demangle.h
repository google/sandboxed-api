// Copyright 2025 Google LLC
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

#ifndef SANDBOXED_API_SANDBOX2_UTIL_DEMANGLE_H_
#define SANDBOXED_API_SANDBOX2_UTIL_DEMANGLE_H_

#include <string>

namespace sandbox2 {

// Note: this function accepts std::string reference instead of string_view
// b/c we will need a 0-terminated string, and accepting std::string
// allows to avoid a copy of the string in the case a caller already has
// std::string.
std::string DemangleSymbol(const std::string& maybe_mangled);

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_UTIL_DEMANGLE_H_
