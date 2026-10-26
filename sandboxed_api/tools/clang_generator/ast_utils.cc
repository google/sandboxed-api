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
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "sandboxed_api/tools/clang_generator/diagnostics.h"

namespace sapi::ast {

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

namespace {

// Removes the SANDBOX_* annotation macros that produced `decl`'s sandbox
// annotate attributes. An attribute's own location points into the macro
// expansion, so it is mapped back to the invocation as written in order to
// delete the macro and any arguments along with it.
void RemoveSandboxAnnotations(const clang::Decl* decl,
                              clang::Rewriter& rewriter) {
  const clang::SourceManager& source_manager = rewriter.getSourceMgr();
  for (const clang::Attr* attr : decl->attrs()) {
    const auto* annotate = llvm::dyn_cast<clang::AnnotateAttr>(attr);
    if (annotate == nullptr || annotate->getAnnotation() != "sandbox") {
      continue;
    }
    rewriter.RemoveText(source_manager.getExpansionRange(attr->getRange()));
  }
}

class ThunkRefFinder : public clang::RecursiveASTVisitor<ThunkRefFinder> {
 public:
  explicit ThunkRefFinder(const clang::FunctionDecl* target)
      : target_(target->getCanonicalDecl()) {}

  bool VisitDeclRefExpr(clang::DeclRefExpr* ref) {
    if (ref->getDecl()->getCanonicalDecl() == target_) {
      found_loc_ = ref->getLocation();
      return false;
    }
    return true;
  }

  const std::optional<clang::SourceLocation>& found_loc() const {
    return found_loc_;
  }

 private:
  const clang::FunctionDecl* target_;
  std::optional<clang::SourceLocation> found_loc_;
};

}  // namespace

std::string GetSourceWithoutAnnotations(const clang::Decl* decl) {
  clang::ASTContext& context = decl->getASTContext();
  clang::Rewriter rewriter(context.getSourceManager(), context.getLangOpts());
  RemoveSandboxAnnotations(decl, rewriter);
  return rewriter.getRewrittenText(
      clang::CharSourceRange::getTokenRange(decl->getSourceRange()));
}

absl::StatusOr<std::string> GetThunkSource(const clang::FunctionDecl* decl,
                                           absl::string_view new_name) {
  if (!decl->hasBody()) {
    return "";
  }
  clang::ASTContext& context = decl->getASTContext();

  ThunkRefFinder ref_finder(decl);
  if (!new_name.empty()) {
    // A renamed thunk (such as a host thunk) replaces the target entry point in
    // the generated code, so referencing it by its thunk name anywhere in the
    // translation unit would leave a dangling symbol.
    ref_finder.TraverseDecl(context.getTranslationUnitDecl());
  } else {
    // A non-renamed thunk (such as a sandboxee thunk) is legitimately called
    // from the corresponding host thunk, but cannot recursively reference
    // itself inside its own body.
    ref_finder.TraverseStmt(decl->getBody());
  }
  if (ref_finder.found_loc().has_value()) {
    return MakeStatusWithDiagnostic(
        *ref_finder.found_loc(), absl::StatusCode::kInvalidArgument,
        absl::StrCat("Thunk '", decl->getNameAsString(),
                     "' cannot be referenced"));
  }

  clang::Rewriter rewriter(context.getSourceManager(), context.getLangOpts());

  // Only parameter annotations can appear inside the declaration's source
  // range -- SANDBOX_HOST_THUNK and friends are leading attributes and sit
  // outside it -- but removing those too costs nothing and keeps this correct
  // if the macros ever move.
  RemoveSandboxAnnotations(decl, rewriter);
  for (const clang::ParmVarDecl* param : decl->parameters()) {
    RemoveSandboxAnnotations(param, rewriter);
  }

  if (!new_name.empty()) {
    rewriter.ReplaceText(decl->getNameInfo().getSourceRange(),
                         llvm::StringRef(new_name.data(), new_name.size()));
  }

  return rewriter.getRewrittenText(
      clang::CharSourceRange::getTokenRange(decl->getSourceRange()));
}

std::string GetFunctionDeclaration(const clang::FunctionDecl* decl) {
  std::string decl_str;
  llvm::raw_string_ostream os(decl_str);
  // Printing the declaration without the body.
  // The policy controls how the declaration is printed.
  clang::PrintingPolicy policy(decl->getASTContext().getLangOpts());
  policy.TerseOutput = true;  // This usually suppresses the body
  // Keep the SANDBOX_* annotations out of the signature. Asking the printer
  // to skip non-keyword attributes avoids having to recognize how it chose to
  // spell them -- annotation arguments are printed as opaque pointer values,
  // not source text.
  policy.PolishForDeclaration = true;
  decl->print(os, policy);

  // remove the trailing semicolon if present
  if (!decl_str.empty() && decl_str.back() == ';') {
    decl_str.pop_back();
  }

  return decl_str;
}

const clang::FunctionProtoType* GetFunctorUnderlyingFunctionType(
    clang::QualType type, std::string& template_name) {
  clang::QualType non_ref = type.getNonReferenceType();
  const auto* record_decl = non_ref->getAsRecordDecl();
  if (!record_decl) {
    return nullptr;
  }
  const auto* spec_decl =
      clang::dyn_cast<clang::ClassTemplateSpecializationDecl>(record_decl);
  if (!spec_decl) {
    return nullptr;
  }
  template_name =
      spec_decl->getSpecializedTemplate()->getQualifiedNameAsString();
  if (template_name != "std::function" &&
      template_name != "absl::AnyInvocable") {
    return nullptr;
  }
  // Extract the underlying function proto type from the template arguments
  // (e.g., `void(int, char*)` from `std::function<void(int, char*)>`).
  const auto& args = spec_decl->getTemplateArgs();
  if (args.size() < 1) {
    return nullptr;
  }
  const clang::TemplateArgument& arg = args[0];
  if (arg.getKind() != clang::TemplateArgument::Type) {
    return nullptr;
  }
  return arg.getAsType()->getAs<clang::FunctionProtoType>();
}

const clang::FunctionProtoType* GetFunctionProtoType(clang::QualType type) {
  if (type->isFunctionPointerType()) {
    return type->getPointeeType()->getAs<clang::FunctionProtoType>();
  }
  std::string template_name;
  if (const clang::FunctionProtoType* func_proto_type =
          GetFunctorUnderlyingFunctionType(type, template_name)) {
    return func_proto_type;
  }
  return nullptr;
}

std::optional<clang::FunctionProtoTypeLoc> GetFunctionProtoTypeLoc(
    clang::TypeSourceInfo* tsi) {
  if (tsi == nullptr) {
    return std::nullopt;
  }
  clang::TypeLoc tl = tsi->getTypeLoc();
  // This loop always terminates. Each branch steps into a strictly inner
  // TypeLoc or follows a TypedefTypeLoc to its TypedefNameDecl. Because C and
  // C++ typedefs cannot be forward-declared, a TypedefNameDecl can only
  // reference declarations created earlier, so the TypeLoc graph is a DAG even
  // on malformed input.
  while (true) {
    tl = tl.getUnqualifiedLoc();
    if (auto ptr_tl = tl.getAs<clang::PointerTypeLoc>()) {
      tl = ptr_tl.getPointeeLoc();
    } else if (auto ref_tl = tl.getAs<clang::ReferenceTypeLoc>()) {
      tl = ref_tl.getPointeeLoc();
    } else if (auto paren_tl = tl.getAs<clang::ParenTypeLoc>()) {
      tl = paren_tl.getInnerLoc();
    } else if (auto typedef_tl = tl.getAs<clang::TypedefTypeLoc>()) {
      // Unwrap typedefs to the most recent declaration's TypeSourceInfo so
      // that parameter names and annotations declared on a typedef (including
      // an annotated redeclaration in the sandbox file) remain visible.
      clang::TypeSourceInfo* underlying_tsi = typedef_tl.getTypePtr()
                                                  ->getDecl()
                                                  ->getMostRecentDecl()
                                                  ->getTypeSourceInfo();
      if (underlying_tsi == nullptr) {
        break;
      }
      tl = underlying_tsi->getTypeLoc();
    } else if (auto attr_tl = tl.getAs<clang::AttributedTypeLoc>()) {
      tl = attr_tl.getModifiedLoc();
#if LLVM_VERSION_MAJOR < 22
      // ElaboratedType was removed from the AST in LLVM 22; its qualifier and
      // keyword sugar was folded directly into the underlying types
      // (RecordTypeLoc, TypedefTypeLoc, TemplateSpecializationTypeLoc, etc.),
      // so there is no wrapper TypeLoc to unwrap in LLVM 22+.
    } else if (auto elab_tl = tl.getAs<clang::ElaboratedTypeLoc>()) {
      tl = elab_tl.getNamedTypeLoc();
#endif
    } else if (auto spec_tl =
                   tl.getAs<clang::TemplateSpecializationTypeLoc>()) {
      if (spec_tl.getNumArgs() >= 1) {
        clang::TemplateArgumentLoc arg_loc = spec_tl.getArgLoc(0);
        if (arg_loc.getArgument().getKind() == clang::TemplateArgument::Type) {
          if (clang::TypeSourceInfo* arg_tsi = arg_loc.getTypeSourceInfo()) {
            tl = arg_tsi->getTypeLoc();
            continue;
          }
        }
      }
      break;
    } else {
      break;
    }
  }
  if (auto ftl = tl.getAs<clang::FunctionProtoTypeLoc>()) {
    return ftl;
  }
  return std::nullopt;
}

}  // namespace sapi::ast
