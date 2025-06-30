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

#include "sandboxed_api/tools/clang_generator/symbol_list_emitter.h"

#include <algorithm>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/Type.h"
#include "sandboxed_api/tools/clang_generator/emitter.h"
#include "sandboxed_api/tools/clang_generator/generator.h"

namespace sapi {

absl::StatusOr<std::string> SymbolListEmitter::Emit(
    const GeneratorOptions& options) {
  std::sort(symbols_.begin(), symbols_.end());
  symbols_.erase(std::unique(symbols_.begin(), symbols_.end()), symbols_.end());
  return absl::StrJoin(symbols_, "\n") + "\n";
}

absl::Status SymbolListEmitter::AddFunction(clang::FunctionDecl* decl) {
  symbols_.push_back(
      clang::ASTNameGenerator(decl->getASTContext()).getName(decl));
  return absl::OkStatus();
}

}  // namespace sapi
