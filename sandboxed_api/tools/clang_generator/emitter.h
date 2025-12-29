// Copyright 2020 Google LLC
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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_EMITTER_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_EMITTER_H_

#include <string>
#include <vector>

#include "absl/log/die_if_null.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "sandboxed_api/tools/clang_generator/emitter_base.h"
#include "sandboxed_api/tools/clang_generator/generator.h"

namespace sapi {

// Responsible for emitting the actual textual representation of the generated
// Sandboxed API header.
class Emitter : public EmitterBase {
 public:
  explicit Emitter(const GeneratorOptions* options)
      : EmitterBase(), options_(*ABSL_DIE_IF_NULL(options)) {}

  // Adds a function to the list of functions to be rendered. In addition, it
  // stores the original and SAPI function information for safe drop-in
  // generation.
  absl::Status AddFunction(clang::FunctionDecl* decl) override;

  // Outputs a formatted header for a list of functions and their related types.
  absl::StatusOr<std::string> EmitHeader();
  absl::StatusOr<std::string> EmitSandboxeeSrc();

  // Rendered function bodies, as a vector to preserve source order. This is
  // not strictly necessary, but makes the output look less surprising.
  std::vector<std::string> rendered_functions_ordered_;

  std::vector<std::string> rendered_sandboxee_handler_ordered_;
  std::vector<std::string> rendered_sandboxee_prototypes_ordered_;

 private:
  // Emits the given function `decl` as SAPI function with a leading comment
  // documenting the unsandboxed function signature.
  absl::StatusOr<std::string> DoEmitFunction(const clang::FunctionDecl* decl);

  // Emits the given function `decl` as a sandboxee function stub. This is
  // called for every function that is emitted. The function stub is a
  // function that is called by the sapi runtime when the corresponding
  // function is called.
  absl::StatusOr<std::string> DoEmitSandboxeeStub(
      const clang::FunctionDecl* decl);

  // Emits the given function `decl` as an external symbol prototype
  // declaration.
  absl::StatusOr<std::string> DoEmitPrototypeSandboxeeFunction(
      const clang::FunctionDecl* decl);

  absl::StatusOr<std::string> DoEmitHeader();
  absl::StatusOr<std::string> DoEmitSandboxeeSrc();

  const GeneratorOptions& options_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_EMITTER_H_
