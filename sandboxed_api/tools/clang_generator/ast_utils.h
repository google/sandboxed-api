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

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Type.h"

namespace sapi::ast {

// Given an access path like "foo->baz", returns the member name "baz".
// If there is no "->", returns std::nullopt.
//
// TODO(b/491717148): For now, we don't handle expressions with "." and return
// std::nullopt as well. For example, like "foo->bar.baz" or "foo.bar->baz",
// since we are getting the struct type from "foo", not "foo->bar" or "foo.bar"
// (see `GetStructTypeName`).
std::optional<std::string> MemberNameOfAccessPath(absl::string_view path);

// Given an access path like "foo->baz", returns the parent prefix "foo->".
// If there is no "->", returns std::nullopt.
//
// TODO(b/491717148): For now, we don't handle expressions with "." and return
// std::nullopt as well.
std::optional<std::string> ParentPrefixOfAccessPath(absl::string_view path);

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

}  // namespace sapi::ast

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_AST_UTILS_H_
