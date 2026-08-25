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

#include "sandboxed_api/tools/clang_generator/ast_utils.h"

#include <cstddef>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/raw_ostream.h"
#include "re2/re2.h"

namespace sapi {

std::optional<std::string> MemberNameOfAccessPath(absl::string_view path) {
  size_t dot_pos = path.rfind('.');
  if (dot_pos != absl::string_view::npos) {
    return std::nullopt;
  }
  size_t arrow_pos = path.rfind("->");
  if (arrow_pos != absl::string_view::npos) {
    return std::string(path.substr(arrow_pos + 2));
  }
  return std::nullopt;
}

std::optional<std::string> ParentPrefixOfAccessPath(absl::string_view path) {
  size_t dot_pos = path.rfind('.');
  if (dot_pos != absl::string_view::npos) {
    return std::nullopt;
  }
  size_t arrow_pos = path.rfind("->");
  if (arrow_pos != absl::string_view::npos) {
    return std::string(path.substr(0, arrow_pos + 2));
  }
  return std::nullopt;
}

std::string ResolveContextName(absl::string_view context) {
  if (context == "$return") {
    return "sapi_ret_arg.GetValue()";
  }
  return std::string(context);
}

std::string CompileBindingExpr(absl::string_view context_var,
                               absl::string_view expr, bool locked) {
  std::string result;
  std::string sub_expr(expr);
  size_t last_pos = 0;
  absl::string_view sp(sub_expr);
  RE2 kBindingNameRegex("\\$([a-zA-Z_][a-zA-Z0-9_]*)");
  std::string binding_name;
  std::string lookup_helper =
      locked ? "sapi_internal_get_context_binding_size_locked"
             : "sapi_internal_get_context_binding_size";
  while (RE2::FindAndConsume(&sp, kBindingNameRegex, &binding_name)) {
    size_t match_pos =
        sp.data() - sub_expr.data() - (binding_name.length() + 1);
    absl::StrAppend(&result, sub_expr.substr(last_pos, match_pos - last_pos));
    absl::SubstituteAndAppend(&result, "$0($1, \"$2\")", lookup_helper,
                              context_var, binding_name);
    last_pos = match_pos + binding_name.length() + 1;
  }
  result.append(sub_expr.substr(last_pos));
  return result;
}
std::string getBody(clang::FunctionDecl* decl, bool full_decl) {
  if (!decl->hasBody()) {
    return "";
  }
  clang::SourceManager& source_manager =
      decl->getASTContext().getSourceManager();
  clang::LangOptions lang_opts = decl->getASTContext().getLangOpts();
  clang::SourceRange source_range =
      full_decl ? decl->getSourceRange() : decl->getBody()->getSourceRange();
  return clang::Lexer::getSourceText(
             clang::CharSourceRange::getTokenRange(source_range),
             source_manager, lang_opts)
      .str();
}

std::string getFunctionDeclaration(clang::FunctionDecl* decl) {
  std::string decl_str;
  llvm::raw_string_ostream os(decl_str);
  // Printing the declaration without the body.
  // The policy controls how the declaration is printed.
  clang::PrintingPolicy policy(decl->getASTContext().getLangOpts());
  policy.TerseOutput = true;  // This usually suppresses the body
  decl->print(os, policy);

  // remove the trailing semicolon if present
  if (!decl_str.empty() && decl_str.back() == ';') {
    decl_str.pop_back();
  }

  // Replace expanded clang annotate attributes like, e.g.
  // `[[clang::annotate("sandbox", 0x70337ed8d258, 0x70337ed8d2c0)]]`
  // with empty string.
  RE2::GlobalReplace(&decl_str, R"(\[\[clang::annotate\([^\]]*\)\]\])", "");

  return decl_str;
}

// TODO(cffsmith): Replace this with something that properly parses the function
// AST and replaces the calls correctly.
absl::Status ReplaceCalls(std::string& body, std::string func_name,
                          std::string name) {
  // Use a regex to replace all calls to func_name with the sapi wrapper.
  std::string result;
  std::string pattern = absl::Substitute(R"($0\()", func_name);
  std::string replacement = absl::Substitute("$0_internal(", name);
  if (!RE2::GlobalReplace(&body, pattern, replacement)) {
    return absl::InternalError(
        absl::Substitute("Failed to replace calls to $0.", func_name));
  }

  // Also replace the function declaration name with the original name
  return absl::OkStatus();
}

absl::Status ReplaceDeclaration(std::string& body, std::string old_name,
                                std::string new_name) {
  // Use a regex to replace all calls to func_name with the sapi wrapper.
  std::string result;
  std::string pattern = absl::Substitute(R"($0\()", old_name);
  std::string replacement = absl::Substitute("$0(", new_name);
  // We expect there to be exactly one declaration of the function.
  if (RE2::GlobalReplace(&body, pattern, replacement) != 1) {
    return absl::InternalError(
        absl::Substitute("Failed to replace declaration of $0.", old_name));
  }

  // Also replace the function declaration name with the original name
  return absl::OkStatus();
}

}  // namespace sapi
