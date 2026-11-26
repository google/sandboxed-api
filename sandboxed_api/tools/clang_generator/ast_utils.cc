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
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Token.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "sandboxed_api/tools/clang_generator/diagnostics.h"

namespace sapi::ast {

std::optional<AccessPathParts> SplitAccessPath(absl::string_view path) {
  size_t arrow_pos = path.find("->");
  if (arrow_pos == 0 || arrow_pos == absl::string_view::npos ||
      path.find("->", arrow_pos + 2) != absl::string_view::npos) {
    return std::nullopt;
  }
  if (size_t first_dot = path.find('.');
      first_dot != absl::string_view::npos && first_dot <= arrow_pos + 1) {
    return std::nullopt;
  }
  absl::string_view rhs = path.substr(arrow_pos + 2);
  if (rhs.empty()) {
    return std::nullopt;
  }
  // Reject empty segments in nested dot chains (e.g., `"foo->.baz"`,
  // `"foo->bar..baz"`, or `"foo->bar."`).
  size_t seg_start = 0;
  while (true) {
    size_t dot = rhs.find('.', seg_start);
    if (dot == absl::string_view::npos) {
      if (seg_start >= rhs.size()) {
        return std::nullopt;
      }
      break;
    }
    if (dot == seg_start) {
      return std::nullopt;
    }
    seg_start = dot + 1;
  }
  size_t split_end = arrow_pos + 2 + seg_start;
  return AccessPathParts{
      .parent_prefix = path.substr(0, split_end),
      .member_name = path.substr(split_end),
  };
}

namespace {

enum class StructExprRole {
  kSize,
  kBindingContext,
  kBindingSize,
};

// Tokenizes `expr` in Clang raw lexer mode with C++ and `$`-identifier support
// enabled so `$binding` placeholders lex as single `raw_identifier` tokens
// regardless of `-fdollars-in-identifiers`. `expr` must be null-terminated and
// must outlive the returned tokens (whose `getRawIdentifier()` views point into
// `expr`).
std::vector<clang::Token> LexRawTokens(clang::LangOptions lang_opts,
                                       const std::string& expr) {
  lang_opts.CPlusPlus = true;
  lang_opts.DollarIdents = true;
  std::vector<clang::Token> tokens;
  clang::Lexer lexer(clang::SourceLocation::getFromRawEncoding(1), lang_opts,
                     expr.data(), expr.data(), expr.data() + expr.size());
  clang::Token tok;
  while (true) {
    bool at_end = lexer.LexFromRawLexer(tok);
    if (tok.isNot(clang::tok::eof)) {
      tokens.push_back(tok);
    }
    if (at_end || tok.is(clang::tok::eof)) {
      break;
    }
  }
  return tokens;
}

bool IsTagKeyword(const clang::Token& tok) {
  if (!tok.is(clang::tok::raw_identifier)) {
    return false;
  }
  llvm::StringRef id = tok.getRawIdentifier();
  return id == "struct" || id == "union" || id == "enum" || id == "class";
}

// Matches `CompileBindingExpr` in `codegen.cc`: a sole C identifier without `$`
// in `binding_expr` names a `ContextBindingRegistry` string key directly rather
// than evaluating as a C++ variable or struct member.
bool IsSoleBindingKey(absl::string_view expr) {
  if (expr.empty() || absl::StrContains(expr, '$') ||
      !(absl::ascii_isalpha(expr[0]) || expr[0] == '_')) {
    return false;
  }
  for (char c : expr) {
    if (!(absl::ascii_isalnum(c) || c == '_')) {
      return false;
    }
  }
  return true;
}

bool IsValidDollarPlaceholder(absl::string_view ident) {
  if (ident.size() < 2 || ident[0] != '$' ||
      !(absl::ascii_isalpha(ident[1]) || ident[1] == '_')) {
    return false;
  }
  for (size_t i = 2; i < ident.size(); ++i) {
    if (!(absl::ascii_isalnum(ident[i]) || ident[i] == '_')) {
      return false;
    }
  }
  return true;
}

bool IsMemberOfRecord(const clang::RecordDecl& record_decl,
                      clang::DeclarationName decl_name) {
  const clang::RecordDecl* def = record_decl.getDefinition();
  if (def == nullptr) {
    return false;
  }
  if (!def->lookup(decl_name).empty()) {
    return true;
  }
  if (const auto* cxx_record = llvm::dyn_cast<clang::CXXRecordDecl>(def)) {
    for (const clang::CXXBaseSpecifier& base : cxx_record->bases()) {
      if (const clang::RecordDecl* base_decl =
              base.getType()->getAsRecordDecl();
          base_decl != nullptr && IsMemberOfRecord(*base_decl, decl_name)) {
        return true;
      }
    }
  }
  return false;
}

bool LookupInDeclContextAndUsingDirectives(
    const clang::DeclContext& ctx, clang::DeclarationName decl_name,
    bool require_value_decl,
    llvm::SmallPtrSetImpl<const clang::NamespaceDecl*>& visited_ns) {
  for (const clang::NamedDecl* nd : ctx.lookup(decl_name)) {
    if (!require_value_decl || llvm::isa<clang::ValueDecl>(nd)) {
      return true;
    }
  }
  for (const clang::UsingDirectiveDecl* ud : ctx.using_directives()) {
    const clang::NamespaceDecl* ns = ud->getNominatedNamespace();
    if (ns != nullptr && visited_ns.insert(ns->getCanonicalDecl()).second &&
        LookupInDeclContextAndUsingDirectives(*ns, decl_name,
                                              require_value_decl, visited_ns)) {
      return true;
    }
  }
  return false;
}

bool IsVisibleInEnclosingScopes(const clang::RecordDecl& def,
                                clang::DeclarationName decl_name,
                                bool require_value_decl) {
  llvm::SmallPtrSet<const clang::NamespaceDecl*, 8> visited_ns;
  for (const clang::DeclContext* ctx = def.getDeclContext(); ctx != nullptr;
       ctx = ctx->getParent()) {
    if (LookupInDeclContextAndUsingDirectives(*ctx, decl_name,
                                              require_value_decl, visited_ns)) {
      return true;
    }
  }
  return false;
}

absl::Status ValidateStructMemberExprImpl(const clang::RecordDecl& real_record,
                                          absl::string_view field_name,
                                          absl::string_view raw_expr,
                                          StructExprRole role) {
  const clang::RecordDecl* def = real_record.getDefinition();
  if (def == nullptr) {
    return absl::OkStatus();
  }
  absl::string_view expr_label =
      role == StructExprRole::kSize
          ? "size expression"
          : (role == StructExprRole::kBindingContext ? "context expression"
                                                     : "binding expression");
  if (role != StructExprRole::kSize &&
      absl::StripAsciiWhitespace(raw_expr).empty()) {
    return absl::InvalidArgumentError(
        absl::Substitute("field $0 in record $1: $2 cannot be empty",
                         field_name, def->getName().str(), expr_label));
  }
  if (raw_expr.empty()) {
    return absl::OkStatus();
  }

  clang::ASTContext& ast_ctx = def->getASTContext();
  clang::LangOptions lang_opts = ast_ctx.getLangOpts();
  lang_opts.CPlusPlus = true;
  clang::IdentifierTable kw_idents(lang_opts);

  if (role == StructExprRole::kBindingSize && IsSoleBindingKey(raw_expr) &&
      kw_idents.get(llvm::StringRef(raw_expr.data(), raw_expr.size()))
              .getTokenID() == clang::tok::identifier) {
    return absl::OkStatus();
  }

  std::string expr(raw_expr);
  std::vector<clang::Token> tokens = LexRawTokens(lang_opts, expr);
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (!tokens[i].is(clang::tok::raw_identifier)) {
      continue;
    }
    llvm::StringRef raw_ident = tokens[i].getRawIdentifier();
    absl::string_view ident(raw_ident.data(), raw_ident.size());

    const bool preceded_by_member_or_scope =
        i > 0 &&
        tokens[i - 1].isOneOf(clang::tok::period, clang::tok::arrow,
                              clang::tok::periodstar, clang::tok::arrowstar,
                              clang::tok::coloncolon);
    const bool preceded_by_tag = i > 0 && IsTagKeyword(tokens[i - 1]);
    const bool followed_by_scope =
        i + 1 < tokens.size() && tokens[i + 1].is(clang::tok::coloncolon);
    const bool followed_by_member_access =
        i + 1 < tokens.size() &&
        tokens[i + 1].isOneOf(clang::tok::period, clang::tok::arrow,
                              clang::tok::periodstar, clang::tok::arrowstar);

    if (absl::StrContains(ident, '$')) {
      if (role != StructExprRole::kBindingSize) {
        return absl::InvalidArgumentError(absl::Substitute(
            "field $0 in record $1: $2 '$3' cannot contain binding "
            "placeholder '$4'",
            field_name, def->getName().str(), expr_label, raw_expr, ident));
      }
      if (!IsValidDollarPlaceholder(ident) || preceded_by_member_or_scope ||
          followed_by_member_access || followed_by_scope) {
        return absl::InvalidArgumentError(absl::Substitute(
            "field $0 in record $1: $2 '$3' has invalid binding placeholder "
            "'$4'",
            field_name, def->getName().str(), expr_label, raw_expr, ident));
      }
      continue;
    }

    if (preceded_by_member_or_scope || preceded_by_tag || followed_by_scope) {
      continue;
    }

    clang::DeclarationName decl_name(&ast_ctx.Idents.get(raw_ident));
    if (followed_by_member_access) {
      if (!IsNonStaticDataMember(*def, ident) &&
          !IsVisibleInEnclosingScopes(*def, decl_name,
                                      /*require_value_decl=*/true)) {
        return absl::InvalidArgumentError(absl::Substitute(
            "field $0 in record $1: $2 '$3' references unknown member '$4'",
            field_name, def->getName().str(), expr_label, raw_expr, ident));
      }
      continue;
    }

    if (kw_idents.get(raw_ident).getTokenID() != clang::tok::identifier) {
      continue;
    }
    if (!IsMemberOfRecord(*def, decl_name) &&
        !IsVisibleInEnclosingScopes(*def, decl_name,
                                    /*require_value_decl=*/false)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "field $0 in record $1: $2 '$3' references unknown member '$4'",
          field_name, def->getName().str(), expr_label, raw_expr, ident));
    }
  }
  return absl::OkStatus();
}

}  // namespace

bool IsNonStaticDataMember(const clang::RecordDecl& record_decl,
                           absl::string_view name) {
  const clang::RecordDecl* def = record_decl.getDefinition();
  if (def == nullptr) {
    return false;
  }
  clang::ASTContext& ast_ctx = def->getASTContext();
  clang::DeclarationName decl_name(
      &ast_ctx.Idents.get(llvm::StringRef(name.data(), name.size())));
  for (const clang::NamedDecl* nd : def->lookup(decl_name)) {
    if (llvm::isa<clang::FieldDecl, clang::IndirectFieldDecl>(nd)) {
      return true;
    }
  }
  if (const auto* cxx_record = llvm::dyn_cast<clang::CXXRecordDecl>(def)) {
    for (const clang::CXXBaseSpecifier& base : cxx_record->bases()) {
      if (const clang::RecordDecl* base_decl =
              base.getType()->getAsRecordDecl();
          base_decl != nullptr && IsNonStaticDataMember(*base_decl, name)) {
        return true;
      }
    }
  }
  return false;
}

absl::Status ValidateStructMemberSizeExpr(const clang::RecordDecl& real_record,
                                          absl::string_view field_name,
                                          absl::string_view size_expr) {
  return ValidateStructMemberExprImpl(real_record, field_name, size_expr,
                                      StructExprRole::kSize);
}

absl::Status ValidateStructMemberBinding(const clang::RecordDecl& real_record,
                                         absl::string_view field_name,
                                         absl::string_view context_expr,
                                         absl::string_view binding_expr) {
  if (absl::Status status =
          ValidateStructMemberExprImpl(real_record, field_name, context_expr,
                                       StructExprRole::kBindingContext);
      !status.ok()) {
    return status;
  }
  return ValidateStructMemberExprImpl(real_record, field_name, binding_expr,
                                      StructExprRole::kBindingSize);
}

std::string PrefixStructMemberSizeExpr(const clang::RecordDecl& record_decl,
                                       absl::string_view parent_prefix,
                                       absl::string_view size_expr) {
  if (parent_prefix.empty() || size_expr.empty()) {
    return std::string(size_expr);
  }
  const clang::RecordDecl* def = record_decl.getDefinition();
  if (def == nullptr) {
    return std::string(size_expr);
  }

  std::string expr(size_expr);
  std::vector<clang::Token> tokens =
      LexRawTokens(def->getASTContext().getLangOpts(), expr);

  std::string result;
  size_t last_pos = 0;
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (!tokens[i].is(clang::tok::raw_identifier)) {
      continue;
    }
    llvm::StringRef ident = tokens[i].getRawIdentifier();
    // `LexRawTokens` lexes a `$binding` or `$return` placeholder as one
    // identifier. Never prefix it, even if a field has the same spelling.
    if (ident.starts_with('$')) {
      continue;
    }
    const bool preceded_by_member_scope_or_tag =
        i > 0 &&
        (tokens[i - 1].isOneOf(clang::tok::period, clang::tok::arrow,
                               clang::tok::periodstar, clang::tok::arrowstar,
                               clang::tok::coloncolon) ||
         IsTagKeyword(tokens[i - 1]));
    const bool followed_by_scope =
        i + 1 < tokens.size() && tokens[i + 1].is(clang::tok::coloncolon);
    if (preceded_by_member_scope_or_tag || followed_by_scope) {
      continue;
    }
    if (!IsNonStaticDataMember(*def,
                               absl::string_view(ident.data(), ident.size()))) {
      continue;
    }
    size_t tok_offset = ident.data() - expr.data();
    absl::StrAppend(
        &result,
        absl::string_view(expr).substr(last_pos, tok_offset - last_pos),
        parent_prefix);
    last_pos = tok_offset;
  }
  absl::StrAppend(&result, absl::string_view(expr).substr(last_pos));
  return result;
}

std::string PrefixStructMemberBindingExpr(const clang::RecordDecl& record_decl,
                                          absl::string_view parent_prefix,
                                          absl::string_view binding_expr) {
  if (IsSoleBindingKey(binding_expr)) {
    return std::string(binding_expr);
  }
  return PrefixStructMemberSizeExpr(record_decl, parent_prefix, binding_expr);
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
