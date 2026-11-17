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

#ifndef SANDBOXED_API_LWBOX_RUNTIME_SANDBOXEE_REGISTRY_H_
#define SANDBOXED_API_LWBOX_RUNTIME_SANDBOXEE_REGISTRY_H_

#include <initializer_list>

#include "absl/strings/string_view.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/function_call_helper.h"

namespace sapi::lwbox {

using SandboxeeFunctionHandler =
    ::sapi::FuncRet (*)(const ::sapi::FunctionCallPreparer& call);

struct SandboxeeFunctionEntry {
  absl::string_view name;
  SandboxeeFunctionHandler handler;
};

struct SandboxeeSymbolEntry {
  absl::string_view name;
  void* address;
};

// Registers a sandboxed library's generated function handlers and exported
// symbols with the merged LWBox sandboxee dispatcher.
//
// Not thread-safe: must only be called during static initialization before
// `main()` starts serving RPC requests, as the underlying dispatch maps are
// read without synchronization by concurrent RPC worker threads.
void RegisterSandboxeeFunctions(
    std::initializer_list<SandboxeeFunctionEntry> functions,
    std::initializer_list<SandboxeeSymbolEntry> symbols);

}  // namespace sapi::lwbox

#endif  // SANDBOXED_API_LWBOX_RUNTIME_SANDBOXEE_REGISTRY_H_
