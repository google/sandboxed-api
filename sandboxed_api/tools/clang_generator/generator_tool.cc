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

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "llvm/Support/CommandLine.h"
#include "sandboxed_api/tools/clang_generator/compilation_database.h"
#include "sandboxed_api/tools/clang_generator/generator.h"
#include "sandboxed_api/util/file_helpers.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_macros.h"

namespace sapi {
namespace {

static auto* g_tool_category =
    new llvm::cl::OptionCategory("Sandboxed API Options");

static auto* g_common_help =
    new llvm::cl::extrahelp(clang::tooling::CommonOptionsParser::HelpMessage);
static auto* g_extra_help = new llvm::cl::extrahelp(
    "Full documentation at: "
    "<https://developers.google.com/code-sandboxing/sandboxed-api/>\n"
    "Report bugs to <https://github.com/google/sandboxed-api/issues>\n");

// Command line options
static auto* g_sapi_embed_dir = new llvm::cl::opt<std::string>(
    "sapi_embed_dir", llvm::cl::desc("Directory with embedded includes"),
    llvm::cl::cat(*g_tool_category));
static auto* g_sapi_embed_name = new llvm::cl::opt<std::string>(
    "sapi_embed_name", llvm::cl::desc("Identifier of the embed object"),
    llvm::cl::cat(*g_tool_category));
static auto* g_sapi_functions = new llvm::cl::list<std::string>(
    "sapi_functions", llvm::cl::CommaSeparated,
    llvm::cl::desc("List of functions to generate a Sandboxed API for. If "
                   "empty, generates a SAPI for all functions found."),
    llvm::cl::cat(*g_tool_category));
static auto* g_sapi_in = new llvm::cl::list<std::string>(
    "sapi_in", llvm::cl::CommaSeparated,
    llvm::cl::desc("List of input files to analyze (deprecated)"),
    llvm::cl::cat(*g_tool_category));
static auto* g_sapi_isystem = new llvm::cl::opt<std::string>(
    "sapi_isystem",
    llvm::cl::desc("Parameter file with extra system include paths (ignored "
                   "for compatibility)"),
    llvm::cl::cat(*g_tool_category));
static auto* g_sapi_limit_scan_depth = new llvm::cl::opt<bool>(
    "sapi_limit_scan_depth",
    llvm::cl::desc(
        "Whether to only scan for functions in the top-most translation unit"),
    llvm::cl::cat(*g_tool_category));
static auto* g_sapi_name = new llvm::cl::opt<std::string>(
    "sapi_name", llvm::cl::desc("Name of the Sandboxed API library"),
    llvm::cl::cat(*g_tool_category));
static auto* g_sapi_ns = new llvm::cl::opt<std::string>(
    "sapi_ns", llvm::cl::desc("C++ namespace to wrap Sandboxed API class in"),
    llvm::cl::cat(*g_tool_category));
static auto* g_sapi_out = new llvm::cl::opt<std::string>(
    "sapi_out",
    llvm::cl::desc(
        "Output path of the generated header. If empty, simply appends .sapi.h "
        "to the basename of the first source file specified."),
    llvm::cl::cat(*g_tool_category));

}  // namespace

GeneratorOptions GeneratorOptionsFromFlags(
    const std::vector<std::string>& sources) {
  GeneratorOptions options;
  options.work_dir = sapi::file_util::fileops::GetCWD();
  options.set_function_names(*g_sapi_functions);
  for (const auto& input : sources) {
    // Keep absolute paths as is, turn
    options.in_files.insert(
        absl::StartsWith(input, "/")
            ? input
            : sapi::file::JoinPath(options.work_dir, input));
  }
  options.set_limit_scan_depth(*g_sapi_limit_scan_depth);
  options.name = *g_sapi_name;
  options.namespace_name = *g_sapi_ns;
  options.out_file =
      !g_sapi_out->empty() ? *g_sapi_out : GetOutputFilename(sources.front());
  options.embed_dir = *g_sapi_embed_dir;
  options.embed_name = *g_sapi_embed_name;
  return options;
}

absl::Status GeneratorMain(int argc, char* argv[]) {
  auto expected_opt_parser = OptionsParser::create(
      argc, const_cast<const char**>(argv), *sapi::g_tool_category,
      llvm::cl::ZeroOrMore,
      "Generates a Sandboxed API header for C/C++ translation units.");
  if (!expected_opt_parser) {
    return absl::InternalError(llvm::toString(expected_opt_parser.takeError()));
  }
  OptionsParser& opt_parser = expected_opt_parser.get();

  std::vector<std::string> sources = opt_parser.getSourcePathList();
  for (const auto& sapi_in : *sapi::g_sapi_in) {
    sources.push_back(sapi_in);
  }
  if (sources.empty()) {
    return absl::InvalidArgumentError("error: no input files");
  }

  auto options = sapi::GeneratorOptionsFromFlags(sources);
  sapi::Emitter emitter;

  std::unique_ptr<clang::tooling::CompilationDatabase> db =
      FromCxxAjustedCompileCommands(
          NonOwningCompileCommands(opt_parser.getCompilations()));
  clang::tooling::ClangTool tool(*db, sources);

  if (!g_sapi_isystem->empty()) {
    absl::FPrintF(
        stderr,
        "note: ignoring deprecated command-line option: sapi_isystem\n");
  }

  if (int result = tool.run(
          std::make_unique<sapi::GeneratorFactory>(emitter, options).get());
      result != 0) {
    return absl::UnknownError("header generation failed");
  }

  SAPI_ASSIGN_OR_RETURN(std::string header, emitter.EmitHeader(options));

  SAPI_RETURN_IF_ERROR(sapi::file::SetContents(options.out_file, header,
                                               sapi::file::Defaults()));
  return absl::OkStatus();
}

}  // namespace sapi

int main(int argc, char* argv[]) {
  if (absl::Status status = sapi::GeneratorMain(argc, argv); !status.ok()) {
    absl::FPrintF(stderr, "%s\n", status.message());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
