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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_GENERATOR_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_GENERATOR_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/log/die_if_null.h"
#include "absl/strings/string_view.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Serialization/PCHContainerOperations.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "sandboxed_api/tools/clang_generator/emitter.h"
#include "sandboxed_api/tools/clang_generator/emitter_base.h"
#include "sandboxed_api/tools/clang_generator/types.h"

namespace sapi {

struct GeneratorOptions {
  template <typename ContainerT>
  GeneratorOptions& set_function_names(const ContainerT& value) {
    function_names.clear();
    function_names.insert(std::begin(value), std::end(value));
    return *this;
  }

  template <typename ContainerT>
  GeneratorOptions& set_in_files(const ContainerT& value) {
    in_files.clear();
    in_files.insert(std::begin(value), std::end(value));
    return *this;
  }

  GeneratorOptions& set_limit_scan_depth(bool value) {
    limit_scan_depth = value;
    return *this;
  }

  bool has_namespace() const { return !namespace_name.empty(); }

  absl::flat_hash_set<std::string> function_names;
  absl::flat_hash_set<std::string> in_files;
  bool limit_scan_depth = false;
  bool symbol_list_gen = false;

  // Output options
  std::string work_dir;
  std::string name;            // Name of the Sandboxed API
  std::string namespace_name;  // Namespace to wrap the SAPI in
  // Output path of the generated header. Used to build the header include
  // guard.
  std::string out_file = "out_file.cc";
  std::string embed_dir;   // Directory with embedded includes
  std::string embed_name;  // Identifier of the embed object
};

class GeneratorASTVisitor
    : public clang::RecursiveASTVisitor<GeneratorASTVisitor> {
 public:
  explicit GeneratorASTVisitor(const GeneratorOptions& options)
      : options_(options) {}

  bool VisitTypeDecl(clang::TypeDecl* decl);
  bool VisitFunctionDecl(clang::FunctionDecl* decl);

  TypeCollector& type_collector() { return type_collector_; }

  const std::vector<clang::FunctionDecl*>& functions() const {
    return functions_;
  }

 private:
  TypeCollector type_collector_;
  std::vector<clang::FunctionDecl*> functions_;
  const GeneratorOptions& options_;
};

class GeneratorASTConsumer : public clang::ASTConsumer {
 public:
  GeneratorASTConsumer(std::string in_file, EmitterBase& emitter,
                       const GeneratorOptions& options)
      : in_file_(std::move(in_file)), visitor_(options), emitter_(emitter) {}

 private:
  void HandleTranslationUnit(clang::ASTContext& context) override;

  std::string in_file_;
  GeneratorASTVisitor visitor_;
  EmitterBase& emitter_;
};

class GeneratorAction : public clang::ASTFrontendAction {
 public:
  GeneratorAction(EmitterBase* emitter, const GeneratorOptions* options)
      : emitter_(*ABSL_DIE_IF_NULL(emitter)),
        options_(*ABSL_DIE_IF_NULL(options)) {}

 private:
  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
      clang::CompilerInstance&, llvm::StringRef in_file) override {
    return std::make_unique<GeneratorASTConsumer>(std::string(in_file),
                                                  emitter_, options_);
  }

  bool BeginSourceFileAction(clang::CompilerInstance& ci);

  bool hasCodeCompletionSupport() const override { return false; }

  EmitterBase& emitter_;
  const GeneratorOptions& options_;
};

class GeneratorFactory : public clang::tooling::FrontendActionFactory {
 public:
  // Does not take ownership
  GeneratorFactory(EmitterBase* emitter, const GeneratorOptions* options)
      : emitter_(*ABSL_DIE_IF_NULL(emitter)),
        options_(*ABSL_DIE_IF_NULL(options)) {}

 private:
  std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<GeneratorAction>(&emitter_, &options_);
  }

  bool runInvocation(
      std::shared_ptr<clang::CompilerInvocation> invocation,
      clang::FileManager* files,
      std::shared_ptr<clang::PCHContainerOperations> pch_container_ops,
      clang::DiagnosticConsumer* diag_consumer) override;

  EmitterBase& emitter_;
  const GeneratorOptions& options_;
};

// Returns the output filename for the given source file ending in .sapi.h.
std::string GetOutputFilename(absl::string_view source_file);

inline absl::string_view ToStringView(llvm::StringRef ref) {
  return absl::string_view(ref.data(), ref.size());
}

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_GENERATOR_H_
