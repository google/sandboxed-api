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

#include "absl/base/attributes.h"
#include "absl/base/no_destructor.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "sandboxed_api/tools/clang_generator/compilation_database.h"
#include "sandboxed_api/tools/clang_generator/emitter.h"
#include "sandboxed_api/tools/clang_generator/generator.h"
#include "sandboxed_api/tools/clang_generator/sandboxed_library_emitter.h"
#include "sandboxed_api/tools/clang_generator/symbol_list_emitter.h"
#include "sandboxed_api/util/file_helpers.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_macros.h"

namespace sapi {
namespace {

absl::NoDestructor<llvm::cl::OptionCategory> g_tool_category(
    "Sandboxed API Options");

absl::NoDestructor<llvm::cl::extrahelp> g_common_help(
    clang::tooling::CommonOptionsParser::HelpMessage);
absl::NoDestructor<llvm::cl::extrahelp> g_extra_help(
    "Full documentation at: "
    "<https://developers.google.com/code-sandboxing/sandboxed-api>\n"
    "Report bugs to <https://github.com/google/sandboxed-api/issues>\n");

// Command line options
absl::NoDestructor<llvm::cl::opt<std::string>> g_sapi_embed_dir(
    "sapi_embed_dir", llvm::cl::desc("Directory with embedded includes"),
    llvm::cl::cat(*g_tool_category));

absl::NoDestructor<llvm::cl::opt<std::string>> g_sapi_embed_name(
    "sapi_embed_name", llvm::cl::desc("Identifier of the embed object"),
    llvm::cl::cat(*g_tool_category));

absl::NoDestructor<llvm::cl::list<std::string>> g_sapi_functions(
    "sapi_functions", llvm::cl::CommaSeparated,
    llvm::cl::desc("List of functions to generate a Sandboxed API for. If "
                   "empty, generates a SAPI for all functions found."),
    llvm::cl::cat(*g_tool_category));

ABSL_DEPRECATED("Pass the input files directly to the tool.")
absl::NoDestructor<llvm::cl::list<std::string>> g_sapi_in(
    "sapi_in", llvm::cl::CommaSeparated,
    llvm::cl::desc("List of input files to analyze (DEPRECATED)"),
    llvm::cl::cat(*g_tool_category));

ABSL_DEPRECATED("Ignored for compatibility.")
absl::NoDestructor<llvm::cl::opt<std::string>> g_sapi_isystem(
    "sapi_isystem",
    llvm::cl::desc(
        "Parameter file with extra system include paths (DEPRECATED)"),
    llvm::cl::cat(*g_tool_category));

absl::NoDestructor<llvm::cl::opt<bool>> g_sapi_limit_scan_depth(
    "sapi_limit_scan_depth",
    llvm::cl::desc("Whether to only scan for functions "
                   "in the top-most translation unit"),
    llvm::cl::cat(*g_tool_category));

absl::NoDestructor<llvm::cl::opt<std::string>> g_sapi_name(
    "sapi_name", llvm::cl::desc("Name of the Sandboxed API library"),
    llvm::cl::cat(*g_tool_category));

absl::NoDestructor<llvm::cl::opt<std::string>> g_sapi_ns(
    "sapi_ns", llvm::cl::desc("C++ namespace to wrap Sandboxed API class in"),
    llvm::cl::cat(*g_tool_category));

absl::NoDestructor<llvm::cl::opt<std::string>> g_sapi_out(
    "sapi_out",
    llvm::cl::desc("Output path of the generated header. If empty, simply "
                   "appends .sapi.h "
                   "to the basename of the first source file specified."),
    llvm::cl::cat(*g_tool_category));

absl::NoDestructor<llvm::cl::opt<bool>> g_symbol_list_gen(
    "symbol_list_gen",
    llvm::cl::desc("Whether to generate a list of symbols exported from the "
                   "library."),
    llvm::cl::cat(*g_tool_category));

absl::NoDestructor<llvm::cl::opt<bool>> g_sandboxed_library_gen(
    "sandboxed_library_gen",
    llvm::cl::desc("Whether to generate a sandboxed library."),
    llvm::cl::cat(*g_tool_category));

absl::NoDestructor<llvm::cl::opt<std::string>> g_sandboxee_hdr_out(
    "sandboxee_hdr_out",
    llvm::cl::desc("Output path of the generated sandboxed library sandboxee "
                   "header file."),
    llvm::cl::cat(*g_tool_category));

absl::NoDestructor<llvm::cl::opt<std::string>> g_sandboxee_src_out(
    "sandboxee_src_out",
    llvm::cl::desc("Output path of the generated sandboxed library sandboxee "
                   "source file."),
    llvm::cl::cat(*g_tool_category));

absl::NoDestructor<llvm::cl::opt<std::string>> g_host_src_out(
    "host_src_out",
    llvm::cl::desc(
        "Output path of the generated sandboxed library host source file."),
    llvm::cl::cat(*g_tool_category));

}  // namespace

GeneratorOptions GeneratorOptionsFromFlags(
    const std::vector<std::string>& sources) {
  GeneratorOptions options;
  options.work_dir = file_util::fileops::GetCWD();
  options.set_function_names(*g_sapi_functions);
  for (const auto& input : sources) {
    // Keep absolute paths as is, turn
    options.in_files.insert(absl::StartsWith(input, "/")
                                ? input
                                : file::JoinPath(options.work_dir, input));
  }
  options.set_limit_scan_depth(*g_sapi_limit_scan_depth);
  options.name = *g_sapi_name;
  options.namespace_name = *g_sapi_ns;
  options.out_file =
      !g_sapi_out->empty() ? *g_sapi_out : GetOutputFilename(sources.front());
  options.embed_dir = *g_sapi_embed_dir;
  options.embed_name = *g_sapi_embed_name;
  options.symbol_list_gen = *g_symbol_list_gen;
  options.sandboxed_library_gen = *g_sandboxed_library_gen;
  return options;
}

absl::Status GeneratorMain(int argc, char* argv[]) {
  auto expected_opt_parser = OptionsParser::create(
      argc, const_cast<const char**>(argv), *g_tool_category,
      llvm::cl::ZeroOrMore,
      "Generates a Sandboxed API header for C/C++ translation units.");
  if (!expected_opt_parser) {
    return absl::InternalError(llvm::toString(expected_opt_parser.takeError()));
  }
  OptionsParser& opt_parser = expected_opt_parser.get();

  std::vector<std::string> sources = opt_parser.getSourcePathList();
  for (const auto& sapi_in : *g_sapi_in) {  // NOLINT
    sources.push_back(sapi_in);
  }
  if (sources.empty()) {
    return absl::InvalidArgumentError("Error: No input files.");
  }

  auto options = GeneratorOptionsFromFlags(sources);

  std::unique_ptr<clang::tooling::CompilationDatabase> db =
      FromCxxAjustedCompileCommands(
          NonOwningCompileCommands(opt_parser.getCompilations()));
  clang::tooling::ClangTool tool(*db, sources);

  if (!g_sapi_isystem->empty()) {  // NOLINT(deprecated)
    absl::FPrintF(
        stderr,
        "Note: Ignoring deprecated command-line option: sapi_isystem\n");
  }

  if (options.symbol_list_gen) {
    SymbolListEmitter emitter;
    if (int result = tool.run(
            std::make_unique<GeneratorFactory>(&emitter, &options).get());
        result != 0) {
      return absl::UnknownError("Error: Header generation failed.");
    }

    SAPI_ASSIGN_OR_RETURN(std::string header, emitter.Emit(options));
    SAPI_RETURN_IF_ERROR(
        file::SetContents(options.out_file, header, file::Defaults()));
    return absl::OkStatus();
  }

  if (options.sandboxed_library_gen) {
    SandboxedLibraryEmitter emitter;
    if (int result = tool.run(
            std::make_unique<GeneratorFactory>(&emitter, &options).get());
        result != 0) {
      return absl::UnknownError("Error: Header generation failed.");
    }

    SAPI_ASSIGN_OR_RETURN(std::string sandboxee_hdr,
                          emitter.EmitSandboxeeHdr(options));
    SAPI_RETURN_IF_ERROR(file::SetContents(*g_sandboxee_hdr_out, sandboxee_hdr,
                                           file::Defaults()));
    SAPI_ASSIGN_OR_RETURN(std::string sandboxee_src,
                          emitter.EmitSandboxeeSrc(options));
    SAPI_RETURN_IF_ERROR(file::SetContents(*g_sandboxee_src_out, sandboxee_src,
                                           file::Defaults()));
    SAPI_ASSIGN_OR_RETURN(std::string host_src, emitter.EmitHostSrc(options));
    SAPI_RETURN_IF_ERROR(
        file::SetContents(*g_host_src_out, host_src, file::Defaults()));
    return absl::OkStatus();
  }

  // Process SAPI header generation.
  Emitter emitter(&options);
  if (int result = tool.run(
          std::make_unique<GeneratorFactory>(&emitter, &options).get());
      result != 0) {
    return absl::UnknownError("Error: Header generation failed.");
  }

  SAPI_ASSIGN_OR_RETURN(std::string header, emitter.EmitHeader());
  SAPI_RETURN_IF_ERROR(
      file::SetContents(options.out_file, header, file::Defaults()));
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
