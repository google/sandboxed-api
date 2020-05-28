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

#include <string>
#include <vector>

#include "absl/memory/memory.h"
#include "clang/AST/ASTContext.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "llvm/Support/CommandLine.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/tools/clang_generator/generator.h"

namespace sapi {
namespace {

static auto* g_tool_category =
    new llvm::cl::OptionCategory("Sandboxed API Options");

static auto* g_common_help =
    new llvm::cl::extrahelp(clang::tooling::CommonOptionsParser::HelpMessage);
static auto* g_extra_help = new llvm::cl::extrahelp(
    "Full documentation at: <https://developers.google.com/sandboxed-api/>\n"
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
    llvm::cl::desc("List of input files to analyze. Deprecated, use positional "
                   "arguments instead."),
    llvm::cl::cat(*g_tool_category));
static auto* g_sapi_isystem = new llvm::cl::opt<std::string>(
    "sapi_isystem", llvm::cl::desc("Extra system include paths"),
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
        "Ouput path of the generated header. If empty, simply appends .sapi.h "
        "to the basename of the first source file specified."),
    llvm::cl::cat(*g_tool_category));

}  // namespace

GeneratorOptions GeneratorOptionsFromFlags() {
  GeneratorOptions options;
  options.function_names.insert(g_sapi_functions->begin(),
                                g_sapi_functions->end());
  options.work_dir = sandbox2::file_util::fileops::GetCWD();
  options.name = *g_sapi_name;
  options.namespace_name = *g_sapi_ns;
  options.out_file = *g_sapi_out;
  options.embed_dir = *g_sapi_embed_dir;
  options.embed_name = *g_sapi_embed_name;
  return options;
}

}  // namespace sapi

int main(int argc, const char** argv) {
  clang::tooling::CommonOptionsParser opt_parser(
      argc, argv, *sapi::g_tool_category, llvm::cl::ZeroOrMore,
      "Generates a Sandboxed API header for C/C++ translation units.");
  std::vector<std::string> sources = opt_parser.getSourcePathList();
  for (const auto& sapi_in : *sapi::g_sapi_in) {
    sources.push_back(sapi_in);
  }

  clang::tooling::ClangTool tool(opt_parser.getCompilations(), sources);
  return tool.run(absl::make_unique<sapi::GeneratorFactory>(
                      sapi::GeneratorOptionsFromFlags())
                      .get());
}
