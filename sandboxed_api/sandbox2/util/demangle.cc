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

#include "sandboxed_api/sandbox2/util/demangle.h"

#include <cxxabi.h>

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>

namespace sandbox2 {

std::string DemangleSymbol(const std::string& maybe_mangled) {
  int status;
  size_t length;
  std::unique_ptr<char, decltype(&std::free)> symbol(
      abi::__cxa_demangle(maybe_mangled.c_str(), /*output_buffer=*/nullptr,
                          &length, &status),
      std::free);
  if (symbol && status == 0) {
    return std::string(symbol.get(), length);
  }
  return maybe_mangled;
}

}  // namespace sandbox2
