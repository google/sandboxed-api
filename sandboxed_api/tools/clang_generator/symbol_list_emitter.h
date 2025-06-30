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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_SYMBOL_LIST_EMITTER_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_SYMBOL_LIST_EMITTER_H_

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "clang/AST/Decl.h"
#include "sandboxed_api/tools/clang_generator/emitter_base.h"

namespace sapi {

class SymbolListEmitter : public EmitterBase {
 public:
  absl::StatusOr<std::string> Emit(const GeneratorOptions& options);

 private:
  absl::Status AddFunction(clang::FunctionDecl* decl) override;

  std::vector<std::string> symbols_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_SYMBOL_LIST_EMITTER_H_
