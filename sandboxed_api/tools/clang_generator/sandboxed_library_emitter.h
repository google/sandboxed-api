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
  // Pointer argument where the data lives in the sandbox, the host can just
  // treat it as a handle with which the sandbox can do what it will, and knows
  // what it refers to.
  // This pointer will then be invalid in the host.
  kSandboxOpaque,
  // Pointer argument where the data lives in the host, the sandbox can just
  // treat it as a handle with which the host can do what it will, and knows
  // what it refers to.
  // This pointer will then be invalid in the sandbox.
  kHostOpaque,
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

  struct Thunk {
    std::string name;
    std::string body;
    std::string declaration;
  };

  struct Func {
    std::string name;
    ArgPtr ret;  // nullptr for void return type
    std::vector<ArgPtr> args;

    // The optional thunks for this function, if they exist, we need to rewire
    // some code.
    std::optional<Thunk> host_thunk;
    std::optional<Thunk> sandboxee_thunk;

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
                                 const clang::ParmVarDecl* param,
                                 const clang::FunctionDecl* funcDecl);
  absl::StatusOr<ArgPtr> ConvertImpl(absl::string_view name,
                                     clang::QualType type,
                                     Annotations&& annotations);
  absl::StatusOr<Annotations> ParseAnnotations(absl::string_view name,
                                               const clang::ParmVarDecl* param);
  absl::StatusOr<Annotations> ParseAnnotations(
      absl::string_view name, const clang::FunctionDecl* funcDecl);
  std::vector<const Func*> SortedFuncs() const;

  // Returns a vector of pairs of name and type of the used host state
  // variables.
  std::vector<std::pair<std::string, std::string>> HostStateVars() const;

  absl::flat_hash_set<std::string> includes_;
  absl::flat_hash_map<std::string, std::unique_ptr<Func>> funcs_;
  absl::flat_hash_set<std::string> sandbox_funcs_;
  absl::flat_hash_set<std::string> ignore_funcs_;
  absl::flat_hash_map<std::string, std::string> used_funcs_;
  std::optional<std::string> funcs_loc_;
  std::vector<std::string> host_state_vars_;
  std::optional<std::string> host_code_;
  std::optional<std::string> sandboxee_code_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_SANDBOXED_LIBRARY_EMITTER_H_
