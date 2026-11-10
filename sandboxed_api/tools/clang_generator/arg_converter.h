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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_ARG_CONVERTER_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_ARG_CONVERTER_H_

#include <optional>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "sandboxed_api/tools/clang_generator/annotations.h"
#include "sandboxed_api/tools/clang_generator/ir.h"

namespace sapi {

// Library-wide lookup tables passed to the AST-to-IR conversion helpers.
struct ConversionContext {
  const absl::flat_hash_map<std::string, RecordAnnotations>& record_annotations;
  // Callback typedefs declared so far. A callback parameter may only reference
  // a typedef that has already been seen, so this being a snapshot of the
  // declarations preceding the current one is what enforces declare-before-use.
  const absl::flat_hash_map<std::string, CallbackTypedef>& callback_typedefs;
};

// Builds a declarative Function IR from a Clang FunctionDecl and its AST
// annotations.
absl::StatusOr<sapi::ir::Function> ConvertFunctionToIR(
    const clang::FunctionDecl* func_decl, const ConversionContext& conv_ctx);

// Builds the indirect caller for a callback typedef: a function whose first
// parameter is the address of a sandboxee function with the typedef's
// signature, and whose remaining parameters are the typedef's own. See
// `ir::Function::invoker_for_typedef`.
//
// Returns nullopt for a callback typedef that has no reverse direction, which
// today means one that hands back a pointer. The buffer would have to be copied
// out of the sandbox and then kept alive, and `SANDBOX_LIFETIME_OUTER_CALL` --
// the only lifetime a callback typedef can state -- names the enclosing library
// call. A host thunk is called by host code rather than from inside a call into
// the sandbox, so there is no outer call for the buffer to outlive and nothing
// to free it.
//
// `decl` is the typedef's own declaration, and has to be passed rather than
// remembered on `CallbackTypedef`: it is only alive while the file that
// declared it is being parsed.
//
// The typedef's annotations are reused unchanged, and mean the right thing
// without being reinterpreted. A callback typedef says what the callback's
// arguments are from the *caller's* side, so reading them as an ordinary
// function's parameter annotations describes the same copies in the opposite
// direction: `in` forward is "copy into the address space of whoever runs the
// callback", which here is the sandbox, and which is exactly what `in` means on
// a library function's parameter.
absl::StatusOr<std::optional<sapi::ir::Function>>
ConvertCallbackTypedefToInvokerIR(const clang::TypedefNameDecl& decl,
                                  const CallbackTypedef& cb_typedef,
                                  const ConversionContext& conv_ctx);

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_ARG_CONVERTER_H_
