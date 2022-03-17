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

#include "sandboxed_api/tools/clang_generator/compilation_database.h"

#include <algorithm>
#include <memory>

#include "absl/strings/match.h"
#include "absl/strings/strip.h"
#include "clang/Driver/Types.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "llvm/Support/Path.h"

namespace sapi {

class WrappingCompilationDatabase : public clang::tooling::CompilationDatabase {
 public:
  explicit WrappingCompilationDatabase(
      clang::tooling::CompilationDatabase& inner)
      : inner_(&inner) {}

 private:
  std::vector<clang::tooling::CompileCommand> getCompileCommands(
      llvm::StringRef file_path) const override {
    return inner_->getCompileCommands(file_path);
  }

  std::vector<std::string> getAllFiles() const override {
    return inner_->getAllFiles();
  }

  std::vector<clang::tooling::CompileCommand> getAllCompileCommands()
      const override {
    return inner_->getAllCompileCommands();
  }

  clang::tooling::CompilationDatabase* inner_;
};

std::unique_ptr<clang::tooling::CompilationDatabase> NonOwningCompileCommands(
    clang::tooling::CompilationDatabase& inner) {
  return std::make_unique<WrappingCompilationDatabase>(inner);
}

// Returns the command-line argument for setting the highest C language standard
// version for a given C++ standard version. If the specified string does not
// indicate a C++ standard, it is returned unchanged.
std::string CxxStdToCStd(const std::string& arg) {
  absl::string_view std = arg;
  if (!absl::ConsumePrefix(&std, "--std=c++") &&
      !absl::ConsumePrefix(&std, "-std=c++")) {
    return arg;
  }
  if (std == "23" || std == "2z" || std == "20" || std == "2a") {
    return "--std=c17";
  }
  if (std == "17" || std == "1z" || std == "14" || std == "1y") {
    return "--std=c11";
  }
  if (std == "11" || std == "0x") {
    return "--std=c99";
  }
  return "--std=c89";
}

class FromCxxAjustedCompilationDatabase
    : public clang::tooling::CompilationDatabase {
 public:
  explicit FromCxxAjustedCompilationDatabase(
      std::unique_ptr<clang::tooling::CompilationDatabase> inner)
      : inner_(std::move(inner)) {}

  std::vector<clang::tooling::CompileCommand> getCompileCommands(
      llvm::StringRef file_path) const override {
    clang::driver::types::ID id =
        llvm::sys::path::has_extension(file_path)
            ? clang::driver::types::lookupTypeForExtension(
                  llvm::sys::path::extension(file_path).drop_front())
            : clang::driver::types::TY_CXXHeader;

    std::vector<clang::tooling::CompileCommand> cmds =
        inner_->getCompileCommands(file_path);
    for (auto& cmd : cmds) {
      auto& argv = cmd.CommandLine;
      if (clang::driver::types::isCXX(id) ||
          id == clang::driver::types::TY_CHeader) {
        argv[0] = "clang++";
        if (id == clang::driver::types::TY_CHeader) {
          // Parse all headers as C++. Well-behaved headers should have an
          // include guard.
          argv.insert(argv.begin() + 1, {"-x", "c++"});
        }
      } else {
        argv[0] = "clang";
        std::transform(argv.begin(), argv.end(), argv.begin(), CxxStdToCStd);
      }
    }
    return cmds;
  }

  std::vector<std::string> getAllFiles() const override {
    return inner_->getAllFiles();
  }

  std::vector<clang::tooling::CompileCommand> getAllCompileCommands()
      const override {
    return {};
  }

  std::unique_ptr<clang::tooling::CompilationDatabase> inner_;
};

std::unique_ptr<clang::tooling::CompilationDatabase>
FromCxxAjustedCompileCommands(
    std::unique_ptr<clang::tooling::CompilationDatabase> inner) {
  return std::make_unique<FromCxxAjustedCompilationDatabase>(std::move(inner));
}

llvm::Expected<OptionsParser> OptionsParser::create(
    int& argc, const char** argv, llvm::cl::OptionCategory& category,
    llvm::cl::NumOccurrencesFlag occurrences_flag, const char* overview) {
  OptionsParser parser;
  if (llvm::Error err =
          parser.init(argc, argv, category, occurrences_flag, overview);
      err) {
    return err;
  }
  return parser;
}

llvm::Error OptionsParser::init(int& argc, const char** argv,
                                llvm::cl::OptionCategory& category,
                                llvm::cl::NumOccurrencesFlag occurrences_flag,
                                const char* overview) {
  static auto* build_path = new llvm::cl::opt<std::string>(
      "p", llvm::cl::desc("Build path"), llvm::cl::Optional,
      llvm::cl::cat(category), llvm::cl::sub(*llvm::cl::AllSubCommands));

  static auto* source_paths = new llvm::cl::list<std::string>(
      llvm::cl::Positional, llvm::cl::desc("<source0> [... <sourceN>]"),
      occurrences_flag, llvm::cl::cat(category),
      llvm::cl::sub(*llvm::cl::AllSubCommands));

  static auto* args_after = new llvm::cl::list<std::string>(
      "extra-arg",
      llvm::cl::desc(
          "Additional argument to append to the compiler command line"),
      llvm::cl::cat(category), llvm::cl::sub(*llvm::cl::AllSubCommands));

  static auto* args_before = new llvm::cl::list<std::string>(
      "extra-arg-before",
      llvm::cl::desc(
          "Additional argument to prepend to the compiler command line"),
      llvm::cl::cat(category), llvm::cl::sub(*llvm::cl::AllSubCommands));

  llvm::cl::ResetAllOptionOccurrences();

  llvm::cl::HideUnrelatedOptions(category);

  {
    std::string error_message;
    compilations_ =
        clang::tooling::FixedCompilationDatabase::loadFromCommandLine(
            argc, argv, error_message);
    if (!error_message.empty()) {
      error_message.append("\n");
    }

    // Stop initializing if command-line option parsing failed.
    if (llvm::raw_string_ostream os(error_message);
        !llvm::cl::ParseCommandLineOptions(argc, argv, overview, &os)) {
      os.flush();
      return llvm::make_error<llvm::StringError>(
          error_message, llvm::inconvertibleErrorCode());
    }
  }
  llvm::cl::PrintOptionValues();

  source_path_list_ = *source_paths;
  if ((occurrences_flag == llvm::cl::ZeroOrMore ||
       occurrences_flag == llvm::cl::Optional) &&
      source_path_list_.empty()) {
    return llvm::Error::success();
  }
  if (!compilations_) {
    std::string error_message;
    if (!build_path->empty()) {
      compilations_ =
          clang::tooling::CompilationDatabase::autoDetectFromDirectory(
              *build_path, error_message);
    } else {
      compilations_ = clang::tooling::CompilationDatabase::autoDetectFromSource(
          (*source_paths)[0], error_message);
    }
    if (!compilations_) {
      compilations_.reset(new clang::tooling::FixedCompilationDatabase(
          ".", std::vector<std::string>()));
    }
  }
  auto adjusting_compilations =
      std::make_unique<clang::tooling::ArgumentsAdjustingCompilations>(
          std::move(compilations_));
  adjuster_ = getInsertArgumentAdjuster(
      *args_before, clang::tooling::ArgumentInsertPosition::BEGIN);
  adjuster_ = clang::tooling::combineAdjusters(
      std::move(adjuster_),
      getInsertArgumentAdjuster(*args_after,
                                clang::tooling::ArgumentInsertPosition::END));
  adjusting_compilations->appendArgumentsAdjuster(adjuster_);
  compilations_ = std::move(adjusting_compilations);
  return llvm::Error::success();
}

}  // namespace sapi
