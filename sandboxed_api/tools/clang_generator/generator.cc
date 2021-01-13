// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sandboxed_api/tools/clang_generator/generator.h"

#include <fstream>
#include <iostream>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "clang/AST/Type.h"
#include "clang/Format/Format.h"
#include "sandboxed_api/tools/clang_generator/diagnostics.h"
#include "sandboxed_api/tools/clang_generator/emitter.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/status_macros.h"

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

inline absl::string_view ToStringView(llvm::StringRef ref) {
  return absl::string_view(ref.data(), ref.size());
}

}  // namespace

std::string GetOutputFilename(absl::string_view source_file) {
  return ReplaceFileExtension(source_file, ".sapi.h");
}

bool GeneratorASTVisitor::VisitFunctionDecl(clang::FunctionDecl* decl) {
  if (!decl->isCXXClassMember() &&  // Skip classes
      decl->isExternC() &&          // Skip non external functions
      !decl->isTemplated() &&       // Skip function templates
      // Process either all function or just the requested ones
      (options_.function_names.empty() ||
       options_.function_names.count(ToStringView(decl->getName())) > 0)) {
    functions_.push_back(decl);

    collector_.CollectRelatedTypes(decl->getDeclaredReturnType());
    for (const clang::ParmVarDecl* param : decl->parameters()) {
      collector_.CollectRelatedTypes(param->getType());
    }
  }
  return true;
}

void GeneratorASTConsumer::HandleTranslationUnit(clang::ASTContext& context) {
  std::cout << "Processing " << in_file_ << "\n";
  if (!visitor_.TraverseDecl(context.getTranslationUnitDecl())) {
    ReportFatalError(context.getDiagnostics(),
                     context.getTranslationUnitDecl()->getBeginLoc(),
                     "AST traversal exited early");
  }

  for (clang::QualType qual : visitor_.collector_.collected()) {
    emitter_.CollectType(qual);
  }
  for (clang::FunctionDecl* func : visitor_.functions_) {
    emitter_.CollectFunction(func);
  }
}

}  // namespace sapi
