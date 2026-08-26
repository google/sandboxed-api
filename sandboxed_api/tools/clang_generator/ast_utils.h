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
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
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

// Returns the body of the function, or an empty string if it has no body.
// If full_decl is true, the entire function declaration is returned,
// otherwise just the body.
std::string getBody(clang::FunctionDecl* decl, bool full_decl);

// Returns the function declaration without the body, stripped of any
// clang::annotate attributes and trailing semicolon.
std::string getFunctionDeclaration(clang::FunctionDecl* decl);

// TODO(cffsmith): Replace this with something that properly parses the function
// AST and replaces the calls correctly.
absl::Status ReplaceCalls(std::string& body, std::string func_name,
                          std::string name);

// Replaces the declaration name of old_name with new_name in body.
absl::Status ReplaceDeclaration(std::string& body, std::string old_name,
                                std::string new_name);

}  // namespace sapi::ast

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_AST_UTILS_H_
