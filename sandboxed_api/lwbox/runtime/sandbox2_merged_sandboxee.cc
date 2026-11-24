// Copyright 2026 Google LLC
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

// Sandbox2 parts of the merged lightweight sandboxee.

#include "sandboxed_api/lwbox/runtime/sandboxee_registry.h"

#if defined(__x86_64__) || defined(__aarch64__) || defined(__riscv)
// Defined in trampolines.S, which is part of
// `//third_party/sandboxed_api:client`.
extern "C" char sapi_callback_trampolines[];

namespace {

// Lets the host look up the trampolines that call host callbacks.
[[maybe_unused]] const bool kTrampolinesRegistered = [] {
  ::sapi::lwbox::RegisterSandboxeeFunctions(
      {}, {{"sapi_callback_trampolines",
            reinterpret_cast<void*>(sapi_callback_trampolines)}});
  return true;
}();

}  // namespace
#endif
