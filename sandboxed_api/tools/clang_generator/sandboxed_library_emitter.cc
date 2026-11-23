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

#include "absl/container/flat_hash_map.h"
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
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
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
  constexpr absl::string_view kSandboxAnnotationPrefix = "sandbox_annotation_";
  // Guard `getName()` with `getIdentifier() != nullptr` because
  // `NamedDecl::getName()` asserts that the declaration name is a simple
  // identifier (which fails on C++ `operator` overloads).
  if (decl->getIdentifier() != nullptr &&
      decl->getName().starts_with(kSandboxAnnotationPrefix)) {
    return ParseAnnotationWrapperFunc(*decl);
  }

  // Record callback typedef references before filtering out ignored functions
  // so that a typedef referenced only by an ignored function is not reported as
  // unused. Walk all redeclarations because the first declaration in the
  // translation unit (from the library header) may spell out a raw function
  // pointer while a later redeclaration attaches the callback typedef.
  auto record_pointee_use = [&](const clang::ParmVarDecl* p) {
    if (p == nullptr) {
      return;
    }
    if (const clang::TypedefNameDecl* pt_td =
            FindPointeeTypedef(p->getType())) {
      used_pointee_contracts_.insert(pt_td->getNameAsString());
    }
  };
  for (const clang::FunctionDecl* redecl : decl->redecls()) {
    if (const clang::TypedefNameDecl* td =
            FindCallbackTypedef(redecl->getReturnType())) {
      referenced_callback_typedefs_.insert(td->getNameAsString());
    }
    for (const clang::ParmVarDecl* param : redecl->parameters()) {
      if (const clang::TypedefNameDecl* td =
              FindCallbackTypedef(param->getType())) {
        referenced_callback_typedefs_.insert(td->getNameAsString());
      } else {
        record_pointee_use(param);
        if (std::optional<clang::FunctionProtoTypeLoc> ftl =
                ast::GetFunctionProtoTypeLoc(param->getTypeSourceInfo());
            ftl.has_value()) {
          for (unsigned i = 0; i < ftl->getNumParams(); ++i) {
            record_pointee_use(ftl->getParam(i));
          }
        }
      }
    }
  }

  std::string func_name = decl->getNameAsString();
  std::string func_type = decl->getType().getCanonicalType().getAsString();

  bool has_unsupported_annotation = false;
  bool is_host_thunk = false;
  bool is_sandboxee_thunk = false;
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
      is_sandboxee_thunk = true;
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
  // (!sandbox_funcs_.empty()). Sandboxee thunks are exempt from SANDBOX_FUNCS
  // because their target function was already verified in library_ir_ above.
  if (ignore_funcs_.contains(func_name) ||
      (!is_sandboxee_thunk && !sandbox_funcs_.empty() &&
       !sandbox_funcs_.contains(func_name))) {
    return absl::OkStatus();
  }

  ABSL_ASSIGN_OR_RETURN(
      ir::Function ir_func,
      ConvertFunctionToIR(
          decl, ConversionContext{
                    .record_annotations = library_ir_.record_annotations,
                    .callback_typedefs = library_ir_.callback_typedefs,
                    .pointee_contracts = library_ir_.pointee_contracts,
                }));
  auto record_ir_param_typedef = [&](const ir::Parameter& p) {
    if (const auto* cb = p.As<ir::CallbackParam>();
        cb != nullptr && cb->typedef_name.has_value()) {
      used_callback_typedefs_.insert(*cb->typedef_name);
    } else if (const auto* scb = p.As<ir::SandboxeeCallbackParam>();
               scb != nullptr) {
      used_callback_typedefs_.insert(scb->typedef_name);
    }
  };
  if (ir_func.return_value.has_value()) {
    record_ir_param_typedef(*ir_func.return_value);
  }
  for (const ir::Parameter& p : ir_func.parameters) {
    record_ir_param_typedef(p);
  }
  if (is_sandboxee_thunk) {
    // EmitSandboxeeSrc and EmitHostSrc always emit sandboxee thunks with
    // `extern "C"` linkage, so the IR function name must stay unmangled even if
    // the annotation file did not wrap the thunk in an `extern "C"` block.
    ir_func.name = func_name;
  }
  library_ir_.functions.push_back(std::move(ir_func));
  return absl::OkStatus();
}

namespace {

// Extracts the single declaration from a `sandbox_annotation_*` wrapper
// function body (which must contain one `DeclStmt` with one `Decl`).
absl::StatusOr<const clang::Decl*> GetAnnotatedDecl(
    const clang::FunctionDecl& decl) {
  // Omit the macro-generated wrapper function name from the error message; the
  // Clang diagnostic location already points to the user's annotation macro.
  const absl::Status malformed = absl::InvalidArgumentError(
      "a sandbox annotation must contain exactly one declaration");

  const auto* body =
      llvm::dyn_cast_or_null<clang::CompoundStmt>(decl.getBody());
  if (body == nullptr || body->size() != 1) {
    return malformed;
  }
  const auto* decl_stmt = llvm::dyn_cast<clang::DeclStmt>(body->body_front());
  if (decl_stmt == nullptr || !decl_stmt->isSingleDecl() ||
      decl_stmt->getSingleDecl() == nullptr) {
    return malformed;
  }
  return decl_stmt->getSingleDecl();
}

}  // namespace

absl::Status SandboxedLibraryEmitter::ParseAnnotationWrapperFunc(
    const clang::FunctionDecl& decl) {
  ABSL_ASSIGN_OR_RETURN(const clang::Decl* annotated, GetAnnotatedDecl(decl));

  // Dispatch on the inner declaration kind.
  if (const auto* record_decl = llvm::dyn_cast<clang::RecordDecl>(annotated)) {
    return ParseRecordAnnotations(*record_decl);
  }
  return absl::InvalidArgumentError(absl::Substitute(
      "sandbox annotations are only supported on structs, but this annotates a "
      "$0 declaration",
      annotated->getDeclKindName()));
}

absl::Status SandboxedLibraryEmitter::AddTypedef(clang::TypedefNameDecl* decl) {
  clang::SourceManager& source_manager =
      decl->getASTContext().getSourceManager();
  clang::SourceLocation decl_start =
      source_manager.getExpansionLoc(decl->getBeginLoc());
  if (source_manager.isInSystemHeader(decl_start)) {
    return absl::OkStatus();
  }
  if (GetCallbackTypedefProto(*decl) == nullptr) {
    if (HasSandboxAnnotations(*decl) &&
        source_manager.isWrittenInMainFile(decl_start)) {
      return ParsePointeeContract(*decl);
    }
    return absl::OkStatus();
  }
  if (HasSandboxAnnotations(*decl)) {
    if (!source_manager.isWrittenInMainFile(decl_start)) {
      return absl::OkStatus();
    }
    return ParseCallbackTypedef(*decl);
  }
  if (decl != decl->getMostRecentDecl()) {
    return absl::OkStatus();
  }
  if (library_ir_.callback_typedefs.contains(decl->getNameAsString())) {
    return absl::OkStatus();
  }
  // Ignore unannotated header typedefs that are not valid callback typedefs
  // on their own; if a sandboxed function references one, `arg_converter.cc`
  // reports the error at the use site.
  if (!sapi::ParseCallbackTypedef(*decl).ok()) {
    return absl::OkStatus();
  }
  return ParseCallbackTypedef(*decl);
}

absl::Status SandboxedLibraryEmitter::ParseCallbackTypedef(
    const clang::TypedefNameDecl& decl) {
  ABSL_ASSIGN_OR_RETURN(CallbackTypedef cb_typedef,
                        sapi::ParseCallbackTypedef(decl));
  for (const auto& param : cb_typedef.params) {
    if (param.annotations.pointee_contract.has_value()) {
      used_pointee_contracts_.insert(*param.annotations.pointee_contract);
    }
  }
  std::string name = cb_typedef.name;
  auto it = library_ir_.callback_typedefs.find(name);
  if (it != library_ir_.callback_typedefs.end()) {
    if (it->second.has_annotations) {
      return absl::InvalidArgumentError(absl::Substitute(
          "callback typedef $0 is annotated more than once", name));
    }
    it->second = std::move(cb_typedef);
    std::erase_if(library_ir_.functions, [&](const ir::Function& f) {
      return f.invoker_for_typedef == name;
    });
  } else {
    it = library_ir_.callback_typedefs
             .emplace(std::move(name), std::move(cb_typedef))
             .first;
  }

  // Built here, and not once everything has been parsed, because `decl` is
  // only alive while this file's AST is. Typedefs are parsed in source
  // order, so the invokers are appended in a stable one. Not every typedef
  // has one; see ConvertCallbackTypedefToInvokerIR.
  if (absl::StatusOr<InvokerConversion> conversion =
          ConvertCallbackTypedefToInvokerIR(
              decl, it->second,
              ConversionContext{
                  .record_annotations = library_ir_.record_annotations,
                  .callback_typedefs = library_ir_.callback_typedefs,
                  .pointee_contracts = library_ir_.pointee_contracts,
              });
      conversion.ok()) {
    if (conversion->invoker.has_value()) {
      library_ir_.functions.push_back(*std::move(conversion->invoker));
    } else {
      it->second.no_reverse_reason = std::move(conversion->no_reverse_reason);
    }
  } else {
    it->second.no_reverse_reason = std::string(conversion.status().message());
  }
  return absl::OkStatus();
}

absl::Status SandboxedLibraryEmitter::ParsePointeeContract(
    const clang::TypedefNameDecl& decl) {
  ABSL_ASSIGN_OR_RETURN(PointeeContract contract,
                        sapi::ParsePointeeContract(decl));
  if (contract.annotations.pointee_contract.has_value() &&
      !library_ir_.pointee_contracts.contains(
          *contract.annotations.pointee_contract)) {
    contract.annotations.pointee_contract = std::nullopt;
  }
  std::string name = contract.name;
  auto [it, inserted] =
      library_ir_.pointee_contracts.try_emplace(name, std::move(contract));
  if (!inserted) {
    return absl::InvalidArgumentError(absl::Substitute(
        "pointee typedef $0 is annotated more than once", name));
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
  if (const auto* lexical_parent =
          llvm::dyn_cast<clang::FunctionDecl>(decl->getLexicalDeclContext());
      lexical_parent != nullptr && lexical_parent->getIdentifier() != nullptr &&
      lexical_parent->getName().starts_with("sandbox_annotation_")) {
    return absl::OkStatus();
  }
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
  // Report any annotated callback typedefs that are never referenced by a
  // callback parameter (the synthetic invoker generated for a typedef does not
  // count as a use).
  std::vector<std::string> unused_typedefs;
  for (const auto& [name, cb_typedef] : library_ir_.callback_typedefs) {
    if (cb_typedef.has_annotations &&
        !referenced_callback_typedefs_.contains(name)) {
      unused_typedefs.push_back(name);
    }
  }
  if (!unused_typedefs.empty()) {
    std::sort(unused_typedefs.begin(), unused_typedefs.end());
    return absl::InvalidArgumentError(absl::Substitute(
        "unused callback typedefs: $0", absl::StrJoin(unused_typedefs, ", ")));
  }
  std::erase_if(library_ir_.functions, [&](const ir::Function& f) {
    return f.invoker_for_typedef.has_value() &&
           !used_callback_typedefs_.contains(*f.invoker_for_typedef);
  });
  absl::erase_if(library_ir_.callback_typedefs, [&](const auto& entry) {
    return !used_callback_typedefs_.contains(entry.first);
  });

  // Propagate `used_pointee_contracts_` transitively through nested pointee
  // typedef chains (`T***`) starting only from outer contracts actually
  // referenced by functions or callbacks.
  std::vector<std::string> worklist(used_pointee_contracts_.begin(),
                                    used_pointee_contracts_.end());
  while (!worklist.empty()) {
    std::string cur = std::move(worklist.back());
    worklist.pop_back();
    auto it = library_ir_.pointee_contracts.find(cur);
    if (it != library_ir_.pointee_contracts.end() &&
        it->second.annotations.pointee_contract.has_value()) {
      const std::string& inner = *it->second.annotations.pointee_contract;
      if (used_pointee_contracts_.insert(inner).second) {
        worklist.push_back(inner);
      }
    }
  }

  std::vector<std::string> unused_pointee_contracts;
  for (const auto& [name, _] : library_ir_.pointee_contracts) {
    if (!used_pointee_contracts_.contains(name)) {
      unused_pointee_contracts.push_back(name);
    }
  }
  if (!unused_pointee_contracts.empty()) {
    std::sort(unused_pointee_contracts.begin(), unused_pointee_contracts.end());
    return absl::InvalidArgumentError(
        absl::Substitute("unused pointee typedefs: $0",
                         absl::StrJoin(unused_pointee_contracts, ", ")));
  }

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
