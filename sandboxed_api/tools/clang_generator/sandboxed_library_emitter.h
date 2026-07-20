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
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
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

struct SandboxGlobalLifetime {};
struct AliasHostPtrLifetime {
  std::string param_name;
};
using PointerLifetime =
    std::variant<std::monostate, SandboxGlobalLifetime, AliasHostPtrLifetime>;

// Metadata for sized-by annotations for pointers to arrays.

// If a host-owned array is sized by an outparam, the size will be controlled
// by the sandbox. We will need to know the maximum size that the host allocated
// for the outparam to perform bounds-checking.
struct SizedByOutparamData {
  std::string capacity_expr;
};
struct ElemSizedBy {
  std::string expr;
  std::optional<SizedByOutparamData> sized_by_outparam_data;
};
struct ByteSizedBy {
  std::string expr;
  std::optional<SizedByOutparamData> sized_by_outparam_data;
};
struct NullTerminated {};
// Size is derived from an opaque context object (`BindData`).
struct SizedByBinding {
  std::string context;
  // A simple expression involving a binding name (prefixed with '$') and
  // host-computable values (e.g., "$binding_name", or "2 * param *
  // $binding_name").
  std::string binding_expr;
};
using ArraySizedByType = std::variant<std::monostate, ElemSizedBy, ByteSizedBy,
                                      NullTerminated, SizedByBinding>;

// Binding primitive data to a context pointer.
struct BindData {
  std::string context;
  std::string type;
  std::string binding_name;
  std::string host_computable_expr;
};

// Binding an output buffer's lifetime to a context pointer (to be freed during
// "clear").
struct CopyFromAndBindOutPtr {
  std::string context;
  std::string binding_name;
};

// Retaining a parameter buffer and binding its lifetime to a context pointer.
struct RetainAndBind {
  std::string context;
  std::string binding_name;
};

// Collection of context-bound annotations for a single function or argument.
struct ContextBoundAnnotations {
  std::vector<BindData> bind_data;
  std::optional<CopyFromAndBindOutPtr> copy_from_and_bind;
  std::optional<RetainAndBind> retain_and_bind;
  bool clear_bindings = false;
};

// A struct sync annotation for a single access path to a struct pointer
// data member, reachable from a pointer to struct parameter.
// E.g., `p->buff`
// The pointer can represent a single object or an array (which may be sized by
// another member of the struct `p->size`, as described in
// `RecordAnnotations`).
struct StructSync {
  std::string access_path;
  PointerDir ptr_dir;
  ContextBoundAnnotations context_bound;
};

class SandboxedLibraryEmitter : public EmitterBase {
 public:
  class Arg;

  // Annotations that apply to all instances of a given Struct/Class type.
  struct DataMemberAnnotations {
    std::string name;
    // For now, we only support sizing annotations for pointer typed members
    // which represent arrays.
    ArraySizedByType size_type;
    // We also support indicating that a pointer is kSandboxOpaque,
    // and so does not need to be synced beyond a shallow copy.
    std::optional<PointerDir> ptr_dir;
  };

  struct RecordAnnotations {
    std::string name;
    std::vector<DataMemberAnnotations> member_annotations;
  };

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

    ContextBoundAnnotations context_bound;
    std::string EmitPostCallBindData() const;

    ~Func();
  };

  struct Annotations {
    std::optional<PointerDir> ptr_dir;
    ArraySizedByType size_type;
    PointerLifetime lifetime;
    bool shallow_struct_sync = false;
    std::vector<StructSync> struct_sync;

    ContextBoundAnnotations context_bound;

    absl::Status CheckSizeNotSet(absl::string_view other_annotation) const {
      if (std::holds_alternative<std::monostate>(size_type)) {
        return absl::OkStatus();
      }
      return absl::InvalidArgumentError(
          absl::StrCat("Cannot be sized by multiple types: ", other_annotation,
                       " and other"));
    }
    absl::Status SetElemSizedBy(absl::string_view expr) {
      ABSL_RETURN_IF_ERROR(CheckSizeNotSet("elem_sized_by"));
      size_type = ElemSizedBy{std::string(expr)};
      return absl::OkStatus();
    }
    absl::Status SetElemSizedByOutparam(absl::string_view size_expr,
                                        absl::string_view capacity_expr) {
      ABSL_RETURN_IF_ERROR(CheckSizeNotSet("elem_sized_by_outparam"));
      size_type = ElemSizedBy{std::string(size_expr),
                              SizedByOutparamData{std::string(capacity_expr)}};
      return absl::OkStatus();
    }
    absl::Status SetByteSizedBy(absl::string_view expr) {
      ABSL_RETURN_IF_ERROR(CheckSizeNotSet("byte_sized_by"));
      size_type = ByteSizedBy{std::string(expr)};
      return absl::OkStatus();
    }
    absl::Status SetByteSizedByOutparam(absl::string_view size_expr,
                                        absl::string_view capacity_expr) {
      ABSL_RETURN_IF_ERROR(CheckSizeNotSet("byte_sized_by_outparam"));
      size_type = ByteSizedBy{std::string(size_expr),
                              SizedByOutparamData{std::string(capacity_expr)}};
      return absl::OkStatus();
    }
    absl::Status SetNullTerminated() {
      ABSL_RETURN_IF_ERROR(CheckSizeNotSet("null_terminated"));
      size_type = NullTerminated{};
      return absl::OkStatus();
    }
    absl::Status SetSizedByBinding(absl::string_view context,
                                   absl::string_view binding_name) {
      ABSL_RETURN_IF_ERROR(CheckSizeNotSet("sized_by_binding"));
      size_type =
          SizedByBinding{std::string(context), std::string(binding_name)};
      return absl::OkStatus();
    }

    absl::Status CheckLifetimeNotSet(absl::string_view other_annotation) const {
      if (std::holds_alternative<std::monostate>(lifetime)) {
        return absl::OkStatus();
      }
      return absl::InvalidArgumentError(absl::StrCat(
          "Cannot have multiple lifetime annotations: ", other_annotation,
          " and other"));
    }
    absl::Status SetSandboxGlobalLifetime() {
      ABSL_RETURN_IF_ERROR(CheckLifetimeNotSet("lifetime_sandbox_global"));
      lifetime = SandboxGlobalLifetime{};
      return absl::OkStatus();
    }
    absl::Status SetAliasHostPtrLifetime(absl::string_view param_name) {
      ABSL_RETURN_IF_ERROR(CheckLifetimeNotSet("alias_ptr"));
      lifetime = AliasHostPtrLifetime{std::string(param_name)};
      return absl::OkStatus();
    }
  };

  absl::Status AddFunction(clang::FunctionDecl* decl) override;
  absl::Status AddVar(clang::VarDecl* decl) override;
  static void EmitFuncDecl(std::string& out, const Func& func);
  static void EmitWrapperDecl(std::string& out, const Func& func);
  void EmitLibraryHeaders(const GeneratorOptions& options,
                          std::string& out) const;
  void RecordContextBindingSupportNeeded(
      const ContextBoundAnnotations& func_context_bound, const ArgPtr& ret,
      const std::vector<ArgPtr>& args);
  std::string EmitContextBindingsHostSupportCode() const;
  absl::StatusOr<std::string> Finalize(const std::string& body, bool is_header,
                                       bool add_includes) const;
  absl::StatusOr<ArgPtr> Convert(absl::string_view name, clang::QualType type,
                                 const clang::ParmVarDecl* param,
                                 const clang::FunctionDecl* funcDecl);
  absl::StatusOr<ArgPtr> ConvertImpl(const clang::ASTContext& context,
                                     absl::string_view name,
                                     clang::QualType type, bool is_param,
                                     Annotations&& annotations);
  absl::StatusOr<Annotations> ParseAnnotations(absl::string_view name,
                                               const clang::ParmVarDecl* param);
  absl::StatusOr<Annotations> ParseAnnotations(
      absl::string_view name, const clang::FunctionDecl* funcDecl);
  absl::Status CheckParsedAnnotations(absl::string_view name,
                                      const Annotations& annotations,
                                      clang::QualType type) const;
  absl::Status ParseStructSyncAccessPathAnnotations(
      const std::vector<std::string>& annotation_args,
      Annotations& annotations) const;
  absl::Status ParseStructAnnotationWrapperFunc(
      const clang::FunctionDecl& decl);
  absl::Status ParseRecordAnnotations(const clang::RecordDecl& decl);

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
