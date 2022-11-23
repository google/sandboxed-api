// Copyright 2020 Google LLC
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

#include "sandboxed_api/tools/clang_generator/generator.h"

#include <fstream>
#include <iostream>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/strip.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "sandboxed_api/tools/clang_generator/diagnostics.h"
#include "sandboxed_api/tools/clang_generator/emitter.h"

namespace sapi {
namespace {

// Replaces the file extension of a path name.
std::string ReplaceFileExtension(absl::string_view path,
                                 absl::string_view new_extension) {
  auto last_slash = path.rfind('/');
  auto pos = path.rfind('.', last_slash);
  if (pos != absl::string_view::npos && last_slash != absl::string_view::npos) {
    pos += last_slash;
  }
  return absl::StrCat(path.substr(0, pos), new_extension);
}

}  // namespace

std::string GetOutputFilename(absl::string_view source_file) {
  return ReplaceFileExtension(source_file, ".sapi.h");
}

bool GeneratorASTVisitor::VisitTypeDecl(clang::TypeDecl* decl) {
  collector_.RecordOrderedDecl(decl);
  return true;
}

bool GeneratorASTVisitor::VisitFunctionDecl(clang::FunctionDecl* decl) {
  if (decl->isCXXClassMember() ||  // Skip classes
      !decl->isExternC() ||        // Skip non external functions
      decl->isTemplated()          // Skip function templates
  ) {
    return true;
  }

  // Process either all function or just the requested ones
  bool all_functions = options_.function_names.empty();
  if (!all_functions &&
      !options_.function_names.contains(ToStringView(decl->getName()))) {
    return true;
  }

  // Skip Abseil internal functions when all functions are requested. This still
  // allows them to be specified explicitly.
  if (all_functions &&
      absl::StartsWith(decl->getQualifiedNameAsString(), "AbslInternal")) {
    return true;
  }

  clang::SourceManager& source_manager =
      decl->getASTContext().getSourceManager();
  clang::SourceLocation decl_start = decl->getBeginLoc();

  // Skip functions from system headers when all functions are requested. Like
  // above, they can still explicitly be specified.
  if (all_functions && source_manager.isInSystemHeader(decl_start)) {
    return true;
  }

  if (all_functions) {
    const std::string filename(absl::StripPrefix(
        ToStringView(source_manager.getFilename(decl_start)), "./"));
    if (options_.limit_scan_depth && !options_.in_files.contains(filename)) {
      return true;
    }
  }

  functions_.push_back(decl);

  collector_.CollectRelatedTypes(decl->getDeclaredReturnType());
  for (const clang::ParmVarDecl* param : decl->parameters()) {
    collector_.CollectRelatedTypes(param->getType());
  }

  return true;
}

void GeneratorASTConsumer::HandleTranslationUnit(clang::ASTContext& context) {
  if (!visitor_.TraverseDecl(context.getTranslationUnitDecl())) {
    ReportFatalError(context.getDiagnostics(),
                     context.getTranslationUnitDecl()->getBeginLoc(),
                     "AST traversal exited early");
    return;
  }

  // TODO(cblichmann): Move below to emit all functions after traversing TUs.
  emitter_.AddTypeDeclarations(visitor_.collector().GetTypeDeclarations());
  for (clang::FunctionDecl* func : visitor_.functions()) {
    absl::Status status = emitter_.AddFunction(func);
    if (!status.ok()) {
      clang::SourceLocation loc =
          GetDiagnosticLocationFromStatus(status).value_or(func->getBeginLoc());
      if (absl::IsCancelled(status)) {
        ReportWarning(context.getDiagnostics(), loc, status.message());
        continue;
      }
      ReportFatalError(context.getDiagnostics(), loc, status.message());
      break;
    }
  }
}

bool GeneratorFactory::runInvocation(
    std::shared_ptr<clang::CompilerInvocation> invocation,
    clang::FileManager* files,
    std::shared_ptr<clang::PCHContainerOperations> pch_container_ops,
    clang::DiagnosticConsumer* diag_consumer) {
  auto& options = invocation->getPreprocessorOpts();
  // Explicitly ask to define __clang_analyzer__ macro.
  options.SetUpStaticAnalyzer = true;
  for (const auto& def : {
           // Enable code to detect whether it is being SAPI-ized
           "__SAPI__",
           // TODO(b/222241644): Figure out how to deal with intrinsics properly
           // Note: The definitions below just need to parse, they don't need to
           //       compile into useful code.
           "__builtin_ia32_cvtsbf162ss_32=[](auto)->long long{return 0;}",
           "__builtin_ia32_paddsb128=",
           "__builtin_ia32_paddsb256=",
           "__builtin_ia32_paddsb512=",
           "__builtin_ia32_paddsw128=",
           "__builtin_ia32_paddsw256=",
           "__builtin_ia32_paddsw512=",
           "__builtin_ia32_paddusb128=",
           "__builtin_ia32_paddusb256=",
           "__builtin_ia32_paddusb512=",
           "__builtin_ia32_paddusw128=",
           "__builtin_ia32_paddusw256=",
           "__builtin_ia32_paddusw512=",
           "__builtin_ia32_psubsb128=",
           "__builtin_ia32_psubsb256=",
           "__builtin_ia32_psubsb512=",
           "__builtin_ia32_psubsw128=",
           "__builtin_ia32_psubsw256=",
           "__builtin_ia32_psubsw512=",
           "__builtin_ia32_psubusb128=",
           "__builtin_ia32_psubusb256=",
           "__builtin_ia32_psubusb512=",
           "__builtin_ia32_psubusw128=",
           "__builtin_ia32_psubusw256=",
           "__builtin_ia32_psubusw512=",
           "__builtin_ia32_reduce_add_d512=[](auto)->long long{return 0;}",
           "__builtin_ia32_reduce_add_q512=[](auto)->long long{return 0;}",
           "__builtin_ia32_reduce_mul_d512=[](auto)->long long{return 0;}",
           "__builtin_ia32_reduce_mul_q512=[](auto)->long long{return 0;}",
       }) {
    options.addMacroDef(def);
  }
  return FrontendActionFactory::runInvocation(std::move(invocation), files,
                                              std::move(pch_container_ops),
                                              diag_consumer);
}

}  // namespace sapi
