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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_GENERATOR_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_GENERATOR_H_

#include <string>

#include "absl/container/flat_hash_set.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "sandboxed_api/util/statusor.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"
#include "sandboxed_api/tools/clang_generator/types.h"

namespace sapi {

struct GeneratorOptions {
  template <typename ContainerT>
  GeneratorOptions& set_function_names(const ContainerT& value) {
    function_names.clear();
    function_names.insert(std::begin(value), std::end(value));
    return *this;
  }

  bool has_namespace() const { return !namespace_name.empty(); }

  absl::flat_hash_set<std::string> function_names;

  // Output options
  std::string work_dir;
  std::string name;            // Name of the Sandboxed API
  std::string namespace_name;  // Namespace to wrap the SAPI in
  std::string out_file;        // Output path of the generated header
  std::string embed_dir;       // Directory with embedded includes
  std::string embed_name;      // Identifier of the embed object
};

class GeneratorASTVisitor
    : public clang::RecursiveASTVisitor<GeneratorASTVisitor> {
 public:
  bool VisitFunctionDecl(clang::FunctionDecl* decl);

 private:
  friend class GeneratorASTConsumer;
  const GeneratorOptions* options_ = nullptr;

  std::vector<clang::FunctionDecl*> functions_;
  QualTypeSet types_;
};

namespace internal {

sapi::StatusOr<std::string> ReformatGoogleStyle(const std::string& filename,
                                                const std::string& code);

}  // namespace internal

class GeneratorASTConsumer : public clang::ASTConsumer {
 public:
  GeneratorASTConsumer(std::string in_file, const GeneratorOptions* options)
      : in_file_(std::move(in_file)), options_(options) {
    visitor_.options_ = options_;
  }

 private:
  void HandleTranslationUnit(clang::ASTContext& context) override;

  absl::Status GenerateAndSaveHeader();

  std::string in_file_;
  const GeneratorOptions* options_;

  GeneratorASTVisitor visitor_;
};

class GeneratorAction : public clang::ASTFrontendAction {
 public:
  explicit GeneratorAction(const GeneratorOptions* options)
      : options_(options) {}

 private:
  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
      clang::CompilerInstance&, llvm::StringRef in_file) override {
    return absl::make_unique<GeneratorASTConsumer>(std::string(in_file),
                                                   options_);
  }

  bool hasCodeCompletionSupport() const override { return false; }

  const GeneratorOptions* options_;
};

class GeneratorFactory : public clang::tooling::FrontendActionFactory {
 public:
  explicit GeneratorFactory(GeneratorOptions options = {})
      : options_(std::move(options)) {}

 private:
#if LLVM_VERSION_MAJOR >= 10
  std::unique_ptr<clang::FrontendAction> create() override {
    return absl::make_unique<GeneratorAction>(&options_);
  }
#else
  clang::FrontendAction* create() override {
    return new GeneratorAction(&options_);
  }
#endif

  GeneratorOptions options_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_GENERATOR_H_
