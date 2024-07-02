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
#include "absl/strings/str_cat.h"  // sapi::google3-only(internal feature only)
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"  // sapi::google3-only(internal feature only)
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "sandboxed_api/tools/clang_generator/compilation_database.h"
#include "sandboxed_api/tools/clang_generator/emitter.h"
#include "sandboxed_api/tools/clang_generator/generator.h"
#include "sandboxed_api/tools/clang_generator/safe_replacement_emitter.h"  // sapi::google3-only(internal feature only)
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
// sapi::google3-begin(internal feature only)
absl::NoDestructor<llvm::cl::opt<bool>> g_safe_wrapper_gen(
    "safe_wrapper_gen",
    llvm::cl::desc("Whether to generate a safe drop-in replacement library."),
    llvm::cl::cat(*g_tool_category));
absl::NoDestructor<llvm::cl::opt<bool>> g_force_safe_wrapper(
    "force_safe_wrapper",
    llvm::cl::desc("Whether to overwrite an existing safe drop-in "
                   "replacement library in the active workspace."),
    llvm::cl::cat(*g_tool_category));
// sapi::google3-end

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
  // sapi::google3-begin(internal feature only)
  options.safe_wrapper_gen = *g_safe_wrapper_gen;
  options.force_safe_wrapper = *g_force_safe_wrapper;
  // sapi::google3-end
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
  for (const auto& sapi_in : *g_sapi_in) {
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

  if (!g_sapi_isystem->empty()) {
    absl::FPrintF(
        stderr,
        "Note: Ignoring deprecated command-line option: sapi_isystem\n");
  }

  // sapi::google3-begin(internal feature only)
  // Process safe drop-in generation.
  if (options.safe_wrapper_gen) {
    SafeReplacementEmitter safe_emitter;
    std::string base_file_path = file::JoinPath(
        options.embed_dir,
        StringReplace(options.embed_name, "sandboxed_", "safe_", false));
    std::string safe_wrapper_header_path = absl::StrCat(base_file_path, ".h");
    std::string safe_wrapper_implementation_path =
        absl::StrCat(base_file_path, ".cc");

    if (file_util::fileops::Exists(safe_wrapper_header_path,
                                   /*fully_resolve=*/true) ||
        file_util::fileops::Exists(safe_wrapper_implementation_path,
                                   /*fully_resolve=*/true)) {
      if (!options.force_safe_wrapper) {
        return absl::UnknownError(
            "Error: Safe drop-in replacement library already exists. To "
            "overwrite it, use the --force_safe_wrapper option.");
      }
    }

    if (int result = tool.run(
            std::make_unique<GeneratorFactory>(safe_emitter, options).get());
        result != 0) {
      return absl::UnknownError("Error: Header generation failed.");
    }

    SAPI_ASSIGN_OR_RETURN(std::string safe_wrapper_header,
                          safe_emitter.EmitSafeDropInHeader(options));
    SAPI_RETURN_IF_ERROR(file::SetContents(
        safe_wrapper_header_path, safe_wrapper_header, file::Defaults()));

    SAPI_ASSIGN_OR_RETURN(std::string safe_wrapper_implementation,
                          safe_emitter.EmitSafeDropInImplementation(options));
    SAPI_RETURN_IF_ERROR(file::SetContents(safe_wrapper_implementation_path,
                                           safe_wrapper_implementation,
                                           file::Defaults()));

    return absl::OkStatus();
  }
  // sapi::google3-end

  // Process SAPI header generation.
  Emitter emitter;
  if (int result =
          tool.run(std::make_unique<GeneratorFactory>(emitter, options).get());
      result != 0) {
    return absl::UnknownError("Error: Header generation failed.");
  }

  SAPI_ASSIGN_OR_RETURN(std::string header, emitter.EmitHeader(options));
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
