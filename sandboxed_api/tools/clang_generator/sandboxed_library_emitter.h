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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_SANDBOXED_LIBRARY_EMITTER_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_SANDBOXED_LIBRARY_EMITTER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "sandboxed_api/tools/clang_generator/emitter_base.h"

namespace sapi {

enum class PointerDir {
  kIn,
  kOut,
  kInOut,
};

class SandboxedLibraryEmitter : public EmitterBase {
 public:
  class Arg;

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

  ~SandboxedLibraryEmitter();

 private:
  using ArgPtr = std::unique_ptr<Arg>;

  struct Func {
    std::string name;
    ArgPtr ret;  // nullptr for void return type
    std::vector<ArgPtr> args;

    ~Func();
  };

  struct Annotations {
    std::optional<PointerDir> ptr_dir;
    std::optional<std::string> elem_sized_by;
  };

  absl::Status AddFunction(clang::FunctionDecl* decl) override;
  absl::Status AddVar(clang::VarDecl* decl) override;
  static void EmitFuncDecl(std::string& out, const Func& func);
  static void EmitWrapperDecl(std::string& out, const Func& func);
  absl::StatusOr<std::string> Finalize(const std::string& body, bool is_header,
                                       bool add_includes) const;
  absl::StatusOr<ArgPtr> Convert(absl::string_view name, clang::QualType type,
                                 const clang::ParmVarDecl* param);
  absl::StatusOr<ArgPtr> ConvertImpl(absl::string_view name,
                                     clang::QualType type,
                                     Annotations&& annotations);
  absl::StatusOr<Annotations> ParseAnnotations(absl::string_view name,
                                               const clang::ParmVarDecl* param);
  std::vector<const Func*> SortedFuncs() const;

  absl::flat_hash_set<std::string> includes_;
  absl::flat_hash_map<std::string, std::unique_ptr<Func>> funcs_;
  absl::flat_hash_set<std::string> sandbox_funcs_;
  absl::flat_hash_set<std::string> ignore_funcs_;
  absl::flat_hash_map<std::string, std::string> used_funcs_;
  std::optional<std::string> funcs_loc_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_SANDBOXED_LIBRARY_EMITTER_H_
