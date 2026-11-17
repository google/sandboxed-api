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

#include "sandboxed_api/lwbox/runtime/sandboxee_registry.h"

#include <cstdint>
#include <initializer_list>
#include <string>

#include "absl/base/no_destructor.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/function_call_helper.h"
#include "sandboxed_api/var_type.h"

#if defined(__x86_64__) || defined(__aarch64__) || defined(__riscv)
extern "C" char sapi_callback_trampolines[];
#endif

namespace sapi::lwbox {
namespace {

using SandboxeeFunctionMap =
    absl::flat_hash_map<std::string, SandboxeeFunctionHandler>;
using SandboxeeSymbolMap = absl::flat_hash_map<std::string, void*>;

SandboxeeFunctionMap& GetSandboxeeFunctions() {
  static absl::NoDestructor<SandboxeeFunctionMap> functions;
  return *functions;
}

SandboxeeSymbolMap& GetSandboxeeSymbols() {
  static absl::NoDestructor<SandboxeeSymbolMap> symbols([] {
    SandboxeeSymbolMap map;
#if defined(__x86_64__) || defined(__aarch64__) || defined(__riscv)
    map.emplace("sapi_callback_trampolines",
                reinterpret_cast<void*>(sapi_callback_trampolines));
#endif
    return map;
  }());
  return *symbols;
}

}  // namespace

void RegisterSandboxeeFunctions(
    std::initializer_list<SandboxeeFunctionEntry> functions,
    std::initializer_list<SandboxeeSymbolEntry> symbols) {
  SandboxeeFunctionMap& fn_map = GetSandboxeeFunctions();
  for (const SandboxeeFunctionEntry& entry : functions) {
    CHECK(entry.handler != nullptr)
        << "Null sandboxee function handler for: " << entry.name;
    auto [it, inserted] = fn_map.emplace(entry.name, entry.handler);
    CHECK(inserted) << "Duplicate sandboxee function registered: "
                    << entry.name;
  }

  SandboxeeSymbolMap& sym_map = GetSandboxeeSymbols();
  for (const SandboxeeSymbolEntry& entry : symbols) {
    auto [it, inserted] = sym_map.emplace(entry.name, entry.address);
    CHECK(inserted) << "Duplicate sandboxee symbol registered: " << entry.name;
  }
}

}  // namespace sapi::lwbox

namespace sapi::client {

void HandleCallMsg(const ::sapi::FuncCall& call, ::sapi::FuncRet* ret) {
  VLOG(1) << "HandleMsgCall, func: '" << call.func
          << "', # of args: " << call.argc;

  const auto& functions = ::sapi::lwbox::GetSandboxeeFunctions();
  auto it = functions.find(call.func);
  if (ABSL_PREDICT_FALSE(it == functions.end())) {
    LOG(ERROR) << "Function not found: '" << call.func << "'";
    *ret = FuncRet::FromError(Error::kInvalidFunctionName);
    return;
  }

  ::sapi::FunctionCallPreparer preparer(call);
  *ret = it->second(preparer);
}

void HandleSymbolMsg(const char* symname, ::sapi::FuncRet* ret) {
  const auto& symbols = ::sapi::lwbox::GetSandboxeeSymbols();
  auto it = symbols.find(symname);
  if (ABSL_PREDICT_FALSE(it == symbols.end())) {
    LOG(ERROR) << "Symbol not found: '" << symname << "'";
    *ret = FuncRet::FromError(Error::kInvalidFunctionName);
    return;
  }
  ret->int_val = reinterpret_cast<uintptr_t>(it->second);
  ret->success = true;
  ret->ret_type = ::sapi::v::Type::kPointer;
}

}  // namespace sapi::client
