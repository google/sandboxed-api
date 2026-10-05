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

#include "sandboxed_api/tools/clang_generator/sandboxed_library_emitter.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/Casting.h"
#include "sandboxed_api/tools/clang_generator/annotations.h"
#include "sandboxed_api/tools/clang_generator/arg_converter.h"
#include "sandboxed_api/tools/clang_generator/ast_utils.h"
#include "sandboxed_api/tools/clang_generator/codegen.h"
#include "sandboxed_api/tools/clang_generator/generator.h"
#include "sandboxed_api/tools/clang_generator/ir.h"

namespace sapi {

absl::Status SandboxedLibraryEmitter::AddFunction(clang::FunctionDecl* decl) {
  constexpr absl::string_view kSandboxStructAnnotationPrefix =
      "sandbox_struct_annotation_";
  // Guard `getName()` with `getIdentifier() != nullptr` because
  // `NamedDecl::getName()` asserts that the declaration name is a simple
  // identifier (which fails on C++ `operator` overloads).
  if (decl->getIdentifier() != nullptr &&
      decl->getName().starts_with(kSandboxStructAnnotationPrefix)) {
    return ParseStructAnnotationWrapperFunc(*decl);
  }

  std::string func_name = decl->getNameAsString();
  std::string func_type = decl->getType().getAsString();

  bool has_unsupported_annotation = false;
  bool is_host_thunk = false;
  ABSL_ASSIGN_OR_RETURN(std::vector<SandboxAnnotation> annotations,
                        GetSandboxAnnotations(decl));
  for (const auto& ann : annotations) {
    if (ann.name == "unsupported") {
      has_unsupported_annotation = true;
    } else if (ann.name == "host_thunk") {
      if (ann.args.empty()) {
        return absl::NotFoundError(
            "Host thunk doesn't not specify the function name.");
      }
      std::string target_func_name = ann.args[0];
      auto* ir_func = library_ir_.FindFunction(target_func_name);
      if (ir_func == nullptr) {
        return absl::NotFoundError(
            absl::Substitute("Function $0 is not found, but has a host thunk.",
                             target_func_name));
      }

      ABSL_ASSIGN_OR_RETURN(std::string body,
                            ast::GetThunkSource(decl, target_func_name));
      ir_func->host_thunk = ir::ThunkOverride{
          .function_name = func_name,
          .body = std::move(body),
          .declaration = ast::GetFunctionDeclaration(decl),
      };
      is_host_thunk = true;
    } else if (ann.name == "sandboxee_thunk") {
      if (ann.args.empty()) {
        return absl::NotFoundError(
            "Sandboxee thunk doesn't not specify the function name.");
      }
      std::string target_func_name = ann.args[0];
      auto* ir_func = library_ir_.FindFunction(target_func_name);
      if (ir_func == nullptr) {
        return absl::NotFoundError(absl::Substitute(
            "Function $0 is not found, but has a sandboxee thunk.",
            target_func_name));
      }
      ABSL_ASSIGN_OR_RETURN(std::string body, ast::GetThunkSource(decl));
      ir_func->sandboxee_thunk = ir::ThunkOverride{
          .function_name = func_name,
          .body = std::move(body),
          .declaration = ast::GetFunctionDeclaration(decl),
      };
    }
  }

  // Check if this is a duplicate signature or overloaded declaration.
  auto [it, inserted] = used_funcs_.insert({func_name, func_type});
  if (!inserted) {
    if (it->second != func_type) {
      LOG(WARNING) << "Function " << func_name
                   << " has multiple signatures: " << it->second << " and "
                   << func_type;
    } else {
      return absl::OkStatus();
    }
  }

  if (has_unsupported_annotation || is_host_thunk) {
    return absl::OkStatus();
  }

  // Skip functions explicitly excluded via SANDBOX_IGNORE_FUNCS, or not listed
  // in SANDBOX_FUNCS when an explicit allowlist is provided
  // (!sandbox_funcs_.empty()). When SANDBOX_FUNCS is not used, sandbox_funcs_
  // is empty and all non-ignored functions (including sandboxee thunks) are
  // sandboxed.
  if (ignore_funcs_.contains(func_name) ||
      (!sandbox_funcs_.empty() && !sandbox_funcs_.contains(func_name))) {
    return absl::OkStatus();
  }

  ABSL_ASSIGN_OR_RETURN(
      ir::Function ir_func,
      ConvertFunctionToIR(decl, library_ir_.record_annotations));
  library_ir_.functions.push_back(std::move(ir_func));
  return absl::OkStatus();
}

absl::Status SandboxedLibraryEmitter::ParseStructAnnotationWrapperFunc(
    const clang::FunctionDecl& decl) {
  const auto* body =
      llvm::dyn_cast_or_null<clang::CompoundStmt>(decl.getBody());
  if (body == nullptr || body->size() != 1) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Unexpected format for sandbox_struct_annotation_ : $0",
        decl.getName().str()));
  }
  const auto* decl_stmt = llvm::dyn_cast<clang::DeclStmt>(body->body_front());
  if (decl_stmt == nullptr || !decl_stmt->isSingleDecl()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Unexpected format for sandbox_struct_annotation_ : $0",
        decl.getName().str()));
  }
  const auto* record_decl =
      llvm::dyn_cast<clang::RecordDecl>(decl_stmt->getSingleDecl());
  if (record_decl == nullptr) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Unexpected format for sandbox_struct_annotation_ : $0",
        decl.getName().str()));
  }
  return ParseRecordAnnotations(*record_decl);
}

/**
 * Extracts the literal string value from a VarDecl if it exists.
 * Example: constexpr char kFoo[] = "foo"; -> returns "foo"
 */
std::optional<std::string> getStringFromVarDecl(const clang::VarDecl* VD) {
  if (!VD) return std::nullopt;

  // 1. Get the initializer expression (the RHS of the '=')
  const clang::Expr* Init = VD->getAnyInitializer();
  if (!Init) return std::nullopt;

  // 2. Strip away "sugar" nodes like ImplicitCasts,
  // MaterializeTemporaryExpr, or ParenExprs
  const clang::Expr* Unwrapped = Init->IgnoreParenImpCasts();

  // 3. Attempt to cast the expression to a StringLiteral
  if (const auto* SL = clang::dyn_cast<clang::StringLiteral>(Unwrapped)) {
    return SL->getString().str();
  }

  return std::nullopt;
}

absl::Status SandboxedLibraryEmitter::AddVar(clang::VarDecl* decl) {
  ABSL_ASSIGN_OR_RETURN(std::vector<SandboxAnnotation> annotations,
                        GetSandboxAnnotations(decl));
  for (const auto& ann : annotations) {
    if (ann.name == "host_state_var") {
      // Include the trailing semicolon when capturing host state variable
      // source text.
      library_ir_.host_state_vars.push_back(
          absl::StrCat(ast::GetSourceWithoutAnnotations(decl), ";"));
    } else if (ann.name == "host_code") {
      library_ir_.host_code = getStringFromVarDecl(decl);
    } else if (ann.name == "sandboxee_code") {
      library_ir_.sandboxee_code = getStringFromVarDecl(decl);
    }
  }

  constexpr absl::string_view kSandboxFuncs = "sandbox_funcs_";
  constexpr absl::string_view kIgnoreFuncs = "sandbox_ignore_funcs_";
  const bool is_sandbox_funcs = decl->getIdentifier() != nullptr &&
                                decl->getName().starts_with(kSandboxFuncs);
  const bool is_ignore_funcs = decl->getIdentifier() != nullptr &&
                               decl->getName().starts_with(kIgnoreFuncs);
  if (is_sandbox_funcs || is_ignore_funcs) {
    if (funcs_loc_) {
      return absl::AlreadyExistsError(absl::Substitute(
          "Only one of SANDBOX_FUNCS or SANDBOX_IGNORE_FUNCS can be used "
          "per file. Previous annotation was at $0",
          *funcs_loc_));
    }
    clang::SourceManager& source_manager =
        decl->getASTContext().getSourceManager();
    funcs_loc_ = source_manager.getExpansionLoc(decl->getBeginLoc())
                     .printToString(source_manager);
    const auto* init_list =
        llvm::dyn_cast_or_null<clang::InitListExpr>(decl->getAnyInitializer());
    if (init_list == nullptr) {
      return absl::InvalidArgumentError(
          "SANDBOX_FUNCS / SANDBOX_IGNORE_FUNCS must be initialized with an "
          "initializer list of string literals");
    }
    for (const clang::Expr* init : init_list->inits()) {
      std::optional<std::string> eval_str =
          init->tryEvaluateString(decl->getASTContext());
      if (!eval_str.has_value()) {
        return absl::InvalidArgumentError(
            "SANDBOX_FUNCS / SANDBOX_IGNORE_FUNCS elements must be string "
            "literals");
      }
      if (is_sandbox_funcs) {
        sandbox_funcs_.insert(*eval_str);
      } else {
        ignore_funcs_.insert(*eval_str);
      }
    }
  }
  return absl::OkStatus();
}

absl::Status SandboxedLibraryEmitter::PostParseAllFiles() {
  if (!funcs_loc_) {
    return ValidateAndLinkLibraryIR(library_ir_);
  }
  const char* ann = "SANDBOX_FUNCS";
  absl::flat_hash_set<std::string>* funcs = &sandbox_funcs_;
  if (!ignore_funcs_.empty()) {
    ann = "SANDBOX_IGNORE_FUNCS";
    funcs = &ignore_funcs_;
  }
  for (const auto& [func, _] : used_funcs_) {
    funcs->erase(func);
  }
  if (!funcs->empty()) {
    std::vector<std::string> funcs_vec(funcs->begin(), funcs->end());
    std::sort(funcs_vec.begin(), funcs_vec.end());
    return absl::InvalidArgumentError(absl::Substitute(
        "$0: unused $1: $2", *funcs_loc_, ann, absl::StrJoin(funcs_vec, ", ")));
  }
  return ValidateAndLinkLibraryIR(library_ir_);
}

SandboxedLibraryEmitter::~SandboxedLibraryEmitter() = default;

absl::StatusOr<std::string> SandboxedLibraryEmitter::EmitSandboxeeHdr(
    const GeneratorOptions& options) const {
  return sapi::EmitSandboxeeHdr(options, library_ir_, includes_);
}

absl::StatusOr<std::string> SandboxedLibraryEmitter::EmitSandboxeeSrc(
    const GeneratorOptions& options) const {
  return sapi::EmitSandboxeeSrc(options, library_ir_, includes_);
}

absl::StatusOr<std::string> SandboxedLibraryEmitter::EmitSandboxeeMain(
    const GeneratorOptions& options) const {
  return sapi::EmitSandboxeeMain(options, library_ir_);
}

absl::StatusOr<std::string> SandboxedLibraryEmitter::EmitHostSrc(
    const GeneratorOptions& options) const {
  return sapi::EmitHostSrc(options, library_ir_, includes_);
}

}  // namespace sapi
