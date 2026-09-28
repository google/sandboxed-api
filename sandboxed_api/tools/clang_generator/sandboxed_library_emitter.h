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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_SANDBOXED_LIBRARY_EMITTER_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_SANDBOXED_LIBRARY_EMITTER_H_

#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "clang/AST/Decl.h"
#include "sandboxed_api/tools/clang_generator/annotations.h"
#include "sandboxed_api/tools/clang_generator/emitter_base.h"
#include "sandboxed_api/tools/clang_generator/ir.h"

namespace sapi {

class SandboxedLibraryEmitter : public EmitterBase {
 public:
  // Called after parsing of all input files.
  // Can be used to finalize data, or emit errors that can be detected
  // only after seeing all files.
  absl::Status PostParseAllFiles();

  absl::StatusOr<std::string> EmitSandboxeeHdr(
      const GeneratorOptions& options) const;
  absl::StatusOr<std::string> EmitSandboxeeSrc(
      const GeneratorOptions& options) const;
  absl::StatusOr<std::string> EmitSandboxeeMain(
      const GeneratorOptions& options) const;
  absl::StatusOr<std::string> EmitHostSrc(
      const GeneratorOptions& options) const;

  ~SandboxedLibraryEmitter() override;

 private:
  absl::Status AddFunction(clang::FunctionDecl* decl) override;
  absl::Status AddVar(clang::VarDecl* decl) override;

  absl::Status ParseStructAnnotationWrapperFunc(
      const clang::FunctionDecl& decl);
  absl::Status ParseRecordAnnotations(const clang::RecordDecl& decl) {
    ABSL_ASSIGN_OR_RETURN(auto record_annotations,
                          sapi::ParseRecordAnnotations(decl));
    std::string name = record_annotations.name;
    library_ir_.record_annotations[name] = std::move(record_annotations);
    return absl::OkStatus();
  }

  absl::flat_hash_set<std::string> includes_;
  absl::flat_hash_set<std::string> sandbox_funcs_;
  absl::flat_hash_set<std::string> ignore_funcs_;
  absl::flat_hash_map<std::string, std::string> used_funcs_;
  std::optional<std::string> funcs_loc_;
  ir::Library library_ir_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_SANDBOXED_LIBRARY_EMITTER_H_
