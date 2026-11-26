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
#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_AST_UTILS_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_AST_UTILS_H_

#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"

namespace sapi::ast {

// Decomposed struct-sync access path such as `"foo->baz"` (`parent_prefix` =
// `"foo->"`, `member_name` = `"baz"`) or `"foo->bar.baz"` (`parent_prefix` =
// `"foo->bar."`, `member_name` = `"baz"`).
struct AccessPathParts {
  absl::string_view parent_prefix;
  absl::string_view member_name;
};

// Splits a struct-sync access path with a single `"->"` (and optional nested
// value-struct `"."` steps after `"->"`) into `{parent_prefix, member_name}`.
// Returns `std::nullopt` if `path` has zero or multiple `"->"` operators, a
// `"."` before `"->"`, or any empty segment.
std::optional<AccessPathParts> SplitAccessPath(absl::string_view path);

// Returns true if `name` names a non-static data member of `record_decl`
// (including fields of anonymous nested structs/unions and C++ base classes).
bool IsNonStaticDataMember(const clang::RecordDecl& record_decl,
                           absl::string_view name);

// Validates a `size_expr` from a `SANDBOX_ANNOTATE_STRUCT` field annotation
// against `real_record`, rejecting `$` placeholders, unknown member bases
// before `.`/`->`, and unqualified identifiers that are neither members of
// `real_record` nor visible in its enclosing declaration scopes.
absl::Status ValidateStructMemberSizeExpr(const clang::RecordDecl& real_record,
                                          absl::string_view field_name,
                                          absl::string_view size_expr);

// Validates `context_expr` and `binding_expr` from a `SANDBOX_ANNOTATE_STRUCT`
// `sized_by_binding` or `elem_sized_by_binding` field annotation against
// `real_record`. Allows `$name` placeholders (or a single bare binding key
// identifier) in `binding_expr`, while checking sibling field and
// enclosing-scope identifiers in both expressions. Rejects empty expressions
// and `$` placeholders in `context_expr`.
absl::Status ValidateStructMemberBinding(const clang::RecordDecl& real_record,
                                         absl::string_view field_name,
                                         absl::string_view context_expr,
                                         absl::string_view binding_expr);

// Rewrites `size_expr` by prepending `parent_prefix` (e.g., "Image->") to
// every unqualified identifier token (excluding `$` placeholders, scope
// qualifiers, member-access suffixes, and elaborated tag names) that resolves
// to a non-static data member of `record_decl`.
std::string PrefixStructMemberSizeExpr(const clang::RecordDecl& record_decl,
                                       absl::string_view parent_prefix,
                                       absl::string_view size_expr);

// Rewrites `binding_expr` from a `sized_by_binding` or `elem_sized_by_binding`
// field annotation like `PrefixStructMemberSizeExpr`, while preserving a sole
// bare binding key identifier (e.g., `"row_size"` without `$`) unprefixed even
// if `record_decl` also has a field with that name.
std::string PrefixStructMemberBindingExpr(const clang::RecordDecl& record_decl,
                                          absl::string_view parent_prefix,
                                          absl::string_view binding_expr);

// Returns the source text of `decl` with the SANDBOX_* annotation macros
// removed.
//
// Note that an annotation written *before* the declaration, as
// SANDBOX_HOST_STATE_VAR is, is not part of the declaration's source range
// and so is not present to begin with.
std::string GetSourceWithoutAnnotations(const clang::Decl* decl);

// Returns the source text of the thunk `decl`, with the SANDBOX_* annotation
// macros removed and, if `new_name` is non-empty, the function renamed to it.
// Returns an empty string if `decl` has no body.
//
// The rewrite is driven by the attributes clang actually parsed rather than by
// patterns over the text, so annotation names inside comments and string
// literals are left alone, and macro arguments containing parentheses are
// handled. Returns an error if a renamed thunk is referenced anywhere in the
// translation unit or if a thunk references itself recursively.
absl::StatusOr<std::string> GetThunkSource(const clang::FunctionDecl* decl,
                                           absl::string_view new_name = {});

// Returns the function declaration without the body, stripped of any
// clang::annotate attributes and trailing semicolon.
std::string GetFunctionDeclaration(const clang::FunctionDecl* decl);

// Given a type, if it is a function pointer type or supported functor type,
// returns the underlying function proto type. Otherwise, returns nullptr.
const clang::FunctionProtoType* GetFunctionProtoType(clang::QualType type);

// Given a type, if it is not a supported functor type, returns nullptr.
// Otherwise, returns the underlying function proto type and sets
// `template_name` (e.g., "std::function", "absl::AnyInvocable").
const clang::FunctionProtoType* GetFunctorUnderlyingFunctionType(
    clang::QualType type, std::string& template_name);

// Unwraps `tsi` (through pointers, references, parentheses, attributes,
// typedefs, and functor template arguments) to a `FunctionProtoTypeLoc`, or
// returns `std::nullopt` if `tsi` does not describe a function prototype.
std::optional<clang::FunctionProtoTypeLoc> GetFunctionProtoTypeLoc(
    clang::TypeSourceInfo* tsi);

}  // namespace sapi::ast

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_AST_UTILS_H_
