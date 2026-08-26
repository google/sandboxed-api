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
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/strings/substitute.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/Type.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/Casting.h"
#include "sandboxed_api/tools/clang_generator/annotations.h"
#include "sandboxed_api/tools/clang_generator/arg.h"
#include "sandboxed_api/tools/clang_generator/ast_utils.h"
#include "sandboxed_api/tools/clang_generator/callback_arg.h"
#include "sandboxed_api/tools/clang_generator/pointer_arg.h"

namespace sapi {

absl::Status SandboxedLibraryEmitter::AddFunction(clang::FunctionDecl* decl) {
  const std::string& func_name = decl->getNameAsString();
  const std::string& func_type =
      decl->getType().getCanonicalType().getAsString();

  // Check if this is a wrapper function containing a record re-definition
  // with annotated data members.
  constexpr absl::string_view kStructAnnotationWrapperFunc =
      "sandbox_struct_annotation_";
  if (absl::StartsWith(func_name, kStructAnnotationWrapperFunc) &&
      decl->getReturnType()->isVoidType() && decl->getNumParams() == 0) {
    return ParseStructAnnotationWrapperFunc(*decl);
  }

  // Check for SANDBOX_HOST_THUNK and SANDBOX_SANDBOXEE_THUNK here.
  // Also check that they are consistent with the other annotations.
  // If it is a HOST thunk, it needs to be attached to the original function.

  bool has_unsupported_annotation = false;
  auto annotations_status = GetSandboxAnnotations(decl);
  if (annotations_status.ok()) {
    for (const auto& ann : *annotations_status) {
      if (ann.name == "unsupported") {
        has_unsupported_annotation = true;
      } else if (ann.name == "host_thunk") {
        if (ann.args.empty()) {
          return absl::NotFoundError(
              "Host thunk doesn't not specify the function name.");
        }
        std::string func_name = ann.args[0];
        if (!funcs_.contains(func_name)) {
          return absl::NotFoundError(absl::Substitute(
              "Function $0 is not found, but has a host thunk.", func_name));
        }
        auto& func = funcs_[func_name];

        // Transform the function to call the generated wrapper for the original
        // function.
        if (!func->sandboxee_thunk.has_value()) {
          return absl::NotFoundError(
              absl::Substitute("Function $0 does not have a sandboxee thunk. "
                               "Cannot verify that the "
                               "host thunk calls the right function.",
                               func_name));
        }

        auto body = StripAnnotations(ast::getBody(decl, true));

        // Replace calls to the sandboxee thunk with the generated wrapper.
        // ABSL_RETURN_IF_ERROR(
        // ast::ReplaceCalls(body, func->sandboxee_thunk->name, func_name));
        auto decl_name = decl->getNameAsString();
        // Replace the name of the function with the original function name.
        ABSL_RETURN_IF_ERROR(
            ast::ReplaceDeclaration(body, decl_name, func_name));

        func->host_thunk = Thunk{
            .name = decl->getNameAsString(),
            .body = body,
            .declaration = ast::getFunctionDeclaration(decl),
        };
      } else if (ann.name == "sandboxee_thunk") {
        if (ann.args.empty()) {
          return absl::NotFoundError(
              "Sandboxee thunk doesn't not specify the function name.");
        }
        std::string func_name = ann.args[0];
        if (!funcs_.contains(func_name)) {
          return absl::NotFoundError(absl::Substitute(
              "Function $0 is not found, but has a sandboxee thunk.",
              func_name));
        }
        auto& func = funcs_[func_name];
        func->sandboxee_thunk = Thunk{
            .name = decl->getNameAsString(),
            .body = StripAnnotations(ast::getBody(decl, true)),
            .declaration = ast::getFunctionDeclaration(decl),
        };
      }
    }
  }

  // Check if this is a thunk and append it to the corresponding function.
  auto [it, inserted] = used_funcs_.insert({func_name, func_type});
  if (!inserted) {
    if (it->second != func_type) {
      // TODO(dvyukov): figure out how we want to handle this case
      // (we see a function with the same name but different signatures).
      // It can mean incorrect signature in out-of-line annotations,
      // but it can also mean just an overloaded C++ function
      // (we have one in our tests).
      LOG(WARNING) << "Function " << func_name
                   << " has multiple signatures: " << it->second << " and "
                   << func_type;
    } else {
      // They are of the same type but since the out-of-line annotations are
      // supplied first, we can skip this function here, this should be the
      // original declaration.
      return absl::OkStatus();
    }
  }

  if (has_unsupported_annotation) {
    return absl::OkStatus();
  }

  if (ignore_funcs_.contains(decl->getNameAsString()) ||
      (!sandbox_funcs_.empty() &&
       !sandbox_funcs_.contains(decl->getNameAsString()))) {
    return absl::OkStatus();
  }

  ArgPtr ret;
  clang::QualType ret_type = decl->getReturnType();
  if (!ret_type->isVoidType()) {
    // Parse return type annotations check function decl for annotations?
    // Consider using annotate_type instead of annotate?.
    ABSL_ASSIGN_OR_RETURN(ret,
                          Convert("sapi_ret_arg", ret_type, nullptr, decl));
    for (const std::string& inc : ret->Includes()) {
      includes_.insert(inc);
    }
    for (const std::string& arg_host_var : ret->HostStateVars()) {
      arg_host_state_vars_.insert(arg_host_var);
    }
  }
  // We may need to parse function-level annotations, even if the return type
  // is void. However, we do not need an ArgPtr for the return value.
  ABSL_ASSIGN_OR_RETURN(Annotations func_decl_annotations,
                        ParseAnnotations(decl->getNameAsString(), decl));
  ContextBoundAnnotations func_context_bound =
      std::move(func_decl_annotations.context_bound);

  std::vector<ArgPtr> args;
  for (size_t i = 0; i < decl->getNumParams(); ++i) {
    const clang::ParmVarDecl* param = decl->getParamDecl(i);
    std::string name = param->getNameAsString();
    if (name.empty()) {
      name = absl::StrFormat("sapi_arg%zu", i);
    }
    clang::QualType type = param->getType();
    ABSL_ASSIGN_OR_RETURN(ArgPtr arg, Convert(name, type, param, nullptr));
    args.push_back(std::move(arg));
  }

  for (const auto& arg : args) {
    ABSL_RETURN_IF_ERROR(arg->LinkArgsIfNeeded(args));
  }

  ABSL_RETURN_IF_ERROR(
      LinkAliasCallbackRelation(decl, func_decl_annotations, ret, args));

  RecordContextBindingSupportNeeded(func_context_bound, ret, args);

  // Determine includes and host state vars, after considering any
  // cross-arg relations (in case we Linking mattered).
  for (const auto& arg : args) {
    for (const std::string& inc : arg->Includes()) {
      includes_.insert(inc);
    }
    for (const std::string& arg_host_var : arg->HostStateVars()) {
      arg_host_state_vars_.insert(arg_host_var);
    }
  }

  std::string name =
      clang::ASTNameGenerator(decl->getASTContext()).getName(decl);
  funcs_[name] = std::make_unique<Func>(name, std::move(ret), std::move(args),
                                        // Thunks will be connected later.
                                        /*host_thunk=*/std::nullopt,
                                        /*sandboxee_thunk=*/std::nullopt,
                                        std::move(func_context_bound));
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
 * Checks if the function needed any of the context-binding-related host vars
 * and code. If so, records that we'll need to emit support code later.
 */
void SandboxedLibraryEmitter::RecordContextBindingSupportNeeded(
    const ContextBoundAnnotations& func_context_bound, const ArgPtr& ret,
    const std::vector<ArgPtr>& args) {
  auto func_has_context_bindings = [&func_context_bound, &ret, &args]() {
    if (!func_context_bound.bind_data.empty()) {
      return true;
    }
    auto arg_ptr_has_context_bindings = [](const ArgPtr& arg) {
      const PointerArg* ptr_arg = dynamic_cast<const PointerArg*>(arg.get());
      return ptr_arg && ptr_arg->HasContextBindings();
    };
    if (ret && arg_ptr_has_context_bindings(ret)) {
      return true;
    }
    for (const auto& arg : args) {
      if (arg_ptr_has_context_bindings(arg)) {
        return true;
      }
    }
    return false;
  };
  if (!func_has_context_bindings()) return;

  // Includes
  includes_.insert("<tuple>");
  includes_.insert("<utility>");
  includes_.insert("<string>");
  includes_.insert(
      absl::Substitute("\"$0absl/base/thread_annotations.h\"", kIncludePrefix));
  includes_.insert(
      absl::Substitute("\"$0absl/container/node_hash_map.h\"", kIncludePrefix));
  includes_.insert(
      absl::Substitute("\"$0absl/synchronization/mutex.h\"", kIncludePrefix));

  // Otherwise, we'll need vars and code as well. Do that separately, since
  // (a) we want to control the order in which they are emitted more carefully
  // (b) it's a bit wasteful to store the same strings into a set for
  //     every function that needs it.
  has_context_bindings_ = true;
}

// If this function has an "alias_callback_return" annotation, checks for the
// existence of the callback parameter, and that the callback parameter returns
// a pointer. If not, returns an error. If so, marks the callback return's
// CallbackArg as being aliased.
absl::Status SandboxedLibraryEmitter::LinkAliasCallbackRelation(
    const clang::FunctionDecl* decl, const Annotations& func_decl_annotations,
    const ArgPtr& ret, const std::vector<ArgPtr>& args) {
  if (!std::holds_alternative<AliasCallbackReturnLifetime>(
          func_decl_annotations.lifetime)) {
    return absl::OkStatus();
  }
  std::string alias_cb_name =
      std::get<AliasCallbackReturnLifetime>(func_decl_annotations.lifetime)
          .callback_param_name;
  if (!ret) {
    return absl::InvalidArgumentError(
        absl::Substitute("function $0: alias_callback_return cannot be "
                         "applied to void function",
                         decl->getNameAsString()));
  }
  bool found_alias_cb = false;
  for (const auto& arg : args) {
    if (arg->GetName() == alias_cb_name) {
      if (auto* cb_arg = dynamic_cast<CallbackArg*>(arg.get())) {
        cb_arg->SetRetValIsAliasForOuterFunctionReturn();
        found_alias_cb = true;
      }
    }
  }
  if (!found_alias_cb) {
    return absl::InvalidArgumentError(absl::Substitute(
        "function $0: alias_callback_return references non-existent or "
        "non-callback parameter $1",
        decl->getNameAsString(), alias_cb_name));
  }
  return absl::OkStatus();
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
  auto annotations_status = GetSandboxAnnotations(decl);
  if (annotations_status.ok()) {
    for (const auto& ann : *annotations_status) {
      if (ann.name == "host_state_var") {
        clang::SourceManager& source_manager =
            decl->getASTContext().getSourceManager();
        clang::LangOptions lang_opts = decl->getASTContext().getLangOpts();
        clang::SourceRange source_range = decl->getSourceRange();

        // We need to include the trailing semicolon, which is not part of the
        // SourceRange usually. But let's try just getting the text.
        std::string text =
            clang::Lexer::getSourceText(
                clang::CharSourceRange::getTokenRange(source_range),
                source_manager, lang_opts)
                .str();
        host_state_vars_.push_back(StripAnnotations(text) + ";");
      } else if (ann.name == "host_code") {
        host_code_ = getStringFromVarDecl(decl);
      } else if (ann.name == "sandboxee_code") {
        sandboxee_code_ = getStringFromVarDecl(decl);
      }
    }
  }

  constexpr absl::string_view kSandboxFuncs = "sandbox_funcs_";
  constexpr absl::string_view kIgnoreFuncs = "sandbox_ignore_funcs_";
  const bool is_sandbox_funcs =
      absl::StartsWith(decl->getNameAsString(), kSandboxFuncs);
  const bool is_ignore_funcs =
      absl::StartsWith(decl->getNameAsString(), kIgnoreFuncs);
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
        llvm::dyn_cast<clang::InitListExpr>(decl->getAnyInitializer());
    for (const clang::Expr* init : init_list->inits()) {
      const std::string func = *init->tryEvaluateString(decl->getASTContext());
      if (is_sandbox_funcs) {
        sandbox_funcs_.insert(func);
      } else {
        ignore_funcs_.insert(func);
      }
    }
  }
  return absl::OkStatus();
}

absl::Status SandboxedLibraryEmitter::PostParseAllFiles() {
  if (!funcs_loc_) {
    return absl::OkStatus();
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
  return absl::OkStatus();
}

std::vector<const SandboxedLibraryEmitter::Func*>
SandboxedLibraryEmitter::SortedFuncs() const {
  std::vector<const Func*> sorted;
  sorted.reserve(funcs_.size());
  for (const auto& func : funcs_) {
    sorted.push_back(func.second.get());
  }
  std::sort(sorted.begin(), sorted.end(),
            [](const Func* a, const Func* b) { return a->name < b->name; });
  sorted.erase(std::unique(sorted.begin(), sorted.end(),
                           [](const Func* a, const Func* b) {
                             return a->name == b->name;
                           }),
               sorted.end());
  return sorted;
}

SandboxedLibraryEmitter::~SandboxedLibraryEmitter() = default;
SandboxedLibraryEmitter::Func::~Func() = default;

}  // namespace sapi
