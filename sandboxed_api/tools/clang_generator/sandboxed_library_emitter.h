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

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "sandboxed_api/tools/clang_generator/annotations.h"
#include "sandboxed_api/tools/clang_generator/arg.h"
#include "sandboxed_api/tools/clang_generator/arg_converter.h"
#include "sandboxed_api/tools/clang_generator/callback_arg.h"
#include "sandboxed_api/tools/clang_generator/emitter_base.h"
#include "sandboxed_api/tools/clang_generator/pointer_arg.h"

namespace sapi {

class SandboxedLibraryEmitter : public EmitterBase {
 public:
  using Arg = sapi::Arg;
  using ArgPtr = sapi::ArgPtr;
  using Annotations = sapi::Annotations;
  using RecordAnnotations = sapi::RecordAnnotations;
  using DataMemberAnnotations = sapi::DataMemberAnnotations;
  using PointerArg = sapi::PointerArg;
  using CallbackArg = sapi::CallbackArg;

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

    ContextBoundAnnotations context_bound;
    std::string EmitPostCallBindData() const;

    ~Func();
  };

  absl::Status AddFunction(clang::FunctionDecl* decl) override;
  absl::Status AddVar(clang::VarDecl* decl) override;
  static void EmitFuncDecl(std::string& out, const Func& func);
  static void EmitWrapperDecl(std::string& out, const Func& func);
  void EmitLibraryHeaders(const GeneratorOptions& options,
                          std::string& out) const;
  absl::StatusOr<std::string> Finalize(const std::string& body, bool is_header,
                                       bool add_includes) const;

  absl::StatusOr<ArgPtr> Convert(absl::string_view name, clang::QualType type,
                                 const clang::ParmVarDecl* param,
                                 const clang::FunctionDecl* funcDecl) {
    return ConvertArg(name, type, param, funcDecl, record_annotations_);
  }
  absl::Status ParseStructAnnotationWrapperFunc(
      const clang::FunctionDecl& decl);
  absl::Status ParseRecordAnnotations(const clang::RecordDecl& decl) {
    ABSL_ASSIGN_OR_RETURN(auto record_annotations,
                          sapi::ParseRecordAnnotations(decl));
    std::string name = record_annotations.name;
    record_annotations_[name] = std::move(record_annotations);
    return absl::OkStatus();
  }

  void RecordContextBindingSupportNeeded(
      const ContextBoundAnnotations& func_context_bound, const ArgPtr& ret,
      const std::vector<ArgPtr>& args);
  std::string EmitContextBindingsHostSupportCode() const;
  absl::Status LinkAliasCallbackRelation(
      const clang::FunctionDecl* decl, const Annotations& func_decl_annotations,
      const ArgPtr& ret, const std::vector<ArgPtr>& args);
  absl::Status LinkAliasParamToCallbackParam(const std::vector<ArgPtr>& args);

  std::vector<const Func*> SortedFuncs() const;

  absl::flat_hash_set<std::string> includes_;
  absl::flat_hash_map<std::string, std::unique_ptr<Func>> funcs_;
  absl::flat_hash_set<std::string> sandbox_funcs_;
  absl::flat_hash_set<std::string> ignore_funcs_;
  absl::flat_hash_map<std::string, std::string> used_funcs_;
  absl::flat_hash_set<std::string> arg_host_state_vars_;
  std::optional<std::string> funcs_loc_;
  std::vector<std::string> host_state_vars_;
  std::optional<std::string> host_code_;
  std::optional<std::string> sandboxee_code_;
  absl::flat_hash_map<std::string, RecordAnnotations> record_annotations_;
  bool has_context_bindings_ = false;
};

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_SANDBOXED_LIBRARY_EMITTER_H_
