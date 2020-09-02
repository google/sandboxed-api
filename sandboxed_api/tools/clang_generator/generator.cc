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
#include "clang/Format/Format.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/tools/clang_generator/diagnostics.h"
#include "sandboxed_api/tools/clang_generator/emitter.h"
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

std::string GetOutputFilename(absl::string_view source_file) {
  return ReplaceFileExtension(source_file, ".sapi.h");
}

inline absl::string_view ToStringView(llvm::StringRef ref) {
  return absl::string_view(ref.data(), ref.size());
}

}  // namespace

bool GeneratorASTVisitor::VisitFunctionDecl(clang::FunctionDecl* decl) {
  if (!decl->isCXXClassMember() &&  // Skip classes
      decl->isExternC() &&          // Skip non external functions
      !decl->isTemplated() &&       // Skip function templates
      // Process either all function or just the requested ones
      (options_->function_names.empty() ||
       options_->function_names.count(ToStringView(decl->getName())) > 0)) {
    functions_.push_back(decl);
    GatherRelatedTypes(decl->getDeclaredReturnType(), &types_);
    for (const clang::ParmVarDecl* param : decl->parameters()) {
      GatherRelatedTypes(param->getType(), &types_);
    }
  }
  return true;
}

namespace internal {

absl::StatusOr<std::string> ReformatGoogleStyle(const std::string& filename,
                                                const std::string& code) {
  // Configure code style based on Google style, but enforce pointer alignment
  clang::format::FormatStyle style =
      clang::format::getGoogleStyle(clang::format::FormatStyle::LK_Cpp);
  style.DerivePointerAlignment = false;
  style.PointerAlignment = clang::format::FormatStyle::PAS_Left;

  clang::tooling::Replacements replacements = clang::format::reformat(
      style, code, llvm::makeArrayRef(clang::tooling::Range(0, code.size())),
      filename);

  llvm::Expected<std::string> formatted_header =
      clang::tooling::applyAllReplacements(code, replacements);
  if (!formatted_header) {
    return absl::InternalError(llvm::toString(formatted_header.takeError()));
  }
  return *formatted_header;
}

}  // namespace internal

absl::Status GeneratorASTConsumer::GenerateAndSaveHeader() {
  const std::string out_file =
      options_->out_file.empty() ? GetOutputFilename(in_file_)
                                 : sandbox2::file_util::fileops::MakeAbsolute(
                                       options_->out_file, options_->work_dir);

  SAPI_ASSIGN_OR_RETURN(const std::string header,
                   EmitHeader(visitor_.functions_, visitor_.types_, *options_));
  SAPI_ASSIGN_OR_RETURN(const std::string formatted_header,
                   internal::ReformatGoogleStyle(in_file_, header));

  std::ofstream os(out_file, std::ios::out | std::ios::trunc);
  os << formatted_header;
  if (!os) {
    return absl::UnknownError("I/O error");
  }
  return absl::OkStatus();
}

void GeneratorASTConsumer::HandleTranslationUnit(clang::ASTContext& context) {
  absl::Status status;
  if (!visitor_.TraverseDecl(context.getTranslationUnitDecl())) {
    status = absl::InternalError("AST traversal exited early");
  } else {
    status = GenerateAndSaveHeader();
  }

  if (!status.ok()) {
    ReportFatalError(context.getDiagnostics(),
                     GetDiagnosticLocationFromStatus(status).value_or(
                         context.getTranslationUnitDecl()->getBeginLoc()),
                     status.message());
  }
}

}  // namespace sapi
