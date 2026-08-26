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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_ANNOTATIONS_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_ANNOTATIONS_H_

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"

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
struct AliasCallbackReturnLifetime {
  std::string callback_param_name;
};
using PointerLifetime =
    std::variant<std::monostate, SandboxGlobalLifetime, AliasHostPtrLifetime,
                 AliasCallbackReturnLifetime>;

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

// Sizing and direction annotations for a single data member in a struct/class.
struct DataMemberAnnotations {
  std::string name;
  ArraySizedByType size_type;
  std::optional<PointerDir> ptr_dir;
};

// Annotations that apply to all instances of a given Struct/Class type.
// For now, we only support sizing annotations for pointer typed members
// which represent arrays.
// We also support indicating that a pointer is kSandboxOpaque,
// and so does not need to be synced beyond a shallow copy.
struct RecordAnnotations {
  std::string name;
  std::vector<DataMemberAnnotations> member_annotations;
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
    return absl::InvalidArgumentError(absl::StrCat(
        "Cannot be sized by multiple types: ", other_annotation, " and other"));
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
                                 absl::string_view binding_expr) {
    ABSL_RETURN_IF_ERROR(CheckSizeNotSet("sized_by_binding"));
    size_type = SizedByBinding{std::string(context), std::string(binding_expr)};
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
  absl::Status SetAliasCallbackReturnLifetime(absl::string_view param_name) {
    ABSL_RETURN_IF_ERROR(CheckLifetimeNotSet("alias_callback_return"));
    lifetime = AliasCallbackReturnLifetime{std::string(param_name)};
    return absl::OkStatus();
  }
};

// Represents a parsed sandbox annotation attribute. For example:
//   [[clang::annotate("sandbox", "elem_sized_by", "size")]]
// is parsed into:
//   name = "elem_sized_by"
//   args = {"size"}
struct SandboxAnnotation {
  std::string name;
  std::vector<std::string> args;
};

// Strips the annotations from the input string.
std::string StripAnnotations(const std::string& input);

// Returns the sandbox annotations for a given decl in their declaration order.
absl::StatusOr<std::vector<SandboxAnnotation>> GetSandboxAnnotations(
    const clang::Decl* decl);

// Parses sandbox annotations for a function return value.
absl::StatusOr<Annotations> ParseAnnotations(
    absl::string_view name, const clang::FunctionDecl* funcDecl);

// Parses sandbox annotations for a function parameter.
absl::StatusOr<Annotations> ParseAnnotations(absl::string_view name,
                                             const clang::ParmVarDecl* param);

// Parses data member annotations for a struct/class definition.
absl::StatusOr<RecordAnnotations> ParseRecordAnnotations(
    const clang::RecordDecl& decl);

std::string ResolveContextName(absl::string_view context);

// Expands the `expr` and replaces all `$binding_name` with context binding
// lookups. If `locked` is true, then the binding mutex is assumed to be held
// already.
std::string CompileBindingExpr(absl::string_view context_var,
                               absl::string_view expr, bool locked);

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_ANNOTATIONS_H_
