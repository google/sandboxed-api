// Copyright 2022 Google LLC
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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_COMPILATION_DATABASE_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_COMPILATION_DATABASE_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"

namespace sapi {

// Returns a CompilationDatabase that redirects to the specified inner database.
std::unique_ptr<clang::tooling::CompilationDatabase> NonOwningCompileCommands(
    clang::tooling::CompilationDatabase& inner);

std::unique_ptr<clang::tooling::CompilationDatabase>
FromCxxAjustedCompileCommands(
    std::unique_ptr<clang::tooling::CompilationDatabase> inner);

// A parser for options common to all command-line Clang tools. This class
// behaves the same as clang::tooling::CommonOptionsParser, except that it won't
// print an error if a compilation database could not be found.
class OptionsParser {
 public:
  static llvm::Expected<OptionsParser> create(
      int& argc, const char** argv, llvm::cl::OptionCategory& category,
      llvm::cl::NumOccurrencesFlag occurrences_flag = llvm::cl::OneOrMore,
      const char* overview = nullptr);

  clang::tooling::CompilationDatabase& getCompilations() {
    return *compilations_;
  }

  const std::vector<std::string>& getSourcePathList() const {
    return source_path_list_;
  }

  clang::tooling::ArgumentsAdjuster getArgumentsAdjuster() { return adjuster_; }

 private:
  OptionsParser() = default;

  llvm::Error init(int& argc, const char** argv,
                   llvm::cl::OptionCategory& category,
                   llvm::cl::NumOccurrencesFlag occurrences_flag,
                   const char* overview);

  std::unique_ptr<clang::tooling::CompilationDatabase> compilations_;
  std::vector<std::string> source_path_list_;
  clang::tooling::ArgumentsAdjuster adjuster_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_COMPILATION_DATABASE_H_
