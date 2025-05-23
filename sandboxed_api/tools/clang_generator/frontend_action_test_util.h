// Copyright 2020 google llc
//
// licensed under the apache license, version 2.0 (the "license");
// you may not use this file except in compliance with the license.
// you may obtain a copy of the license at
//
//     http://www.apache.org/licenses/license-2.0
//
// unless required by applicable law or agreed to in writing, software
// distributed under the license is distributed on an "as is" basis,
// without warranties or conditions of any kind, either express or implied.
// see the license for the specific language governing permissions and
// limitations under the license.

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_FRONTEND_ACTION_TEST_UTIL_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_FRONTEND_ACTION_TEST_UTIL_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "clang/Frontend/FrontendAction.h"

namespace sapi {
namespace internal {

// Returns the contents of the file.
std::string GetTestFileContents(absl::string_view file);

// Sets up a virtual filesystem, adds code files to it, and runs a clang tool
// on it.
absl::Status RunClangTool(
    const std::vector<std::string>& command_line,
    const absl::flat_hash_map<std::string, std::string>& file_contents,
    std::unique_ptr<clang::FrontendAction> action);

}  // namespace internal

class FrontendActionTest : public ::testing::Test {
 protected:
  // Adds code to a virtual filesystem with the given filename.
  void AddCode(const std::string& filename, absl::string_view code) {
    absl::StrAppend(&file_contents_[filename], code);
  }

  // Changes the name of the virtual input file. Useful for special cases where
  // the filenames of compiled sources matter.
  void set_input_file(absl::string_view value) {
    input_file_ = std::string(value);
  }

  // Returns the command line flags for the specified input file.
  virtual std::vector<std::string> GetCommandLineFlagsForTesting(
      absl::string_view input_file);

  // Runs the specified frontend action on file loaded in-memory.
  absl::Status RunFrontendActionOnFile(
      absl::string_view input_file,
      std::unique_ptr<clang::FrontendAction> action) {
    set_input_file(input_file);
    std::string code = internal::GetTestFileContents(input_file);
    return RunFrontendAction(code, std::move(action));
  }

  // Runs the specified frontend action on in-memory source code.
  absl::Status RunFrontendAction(
      absl::string_view code, std::unique_ptr<clang::FrontendAction> action) {
    std::vector<std::string> command_line =
        GetCommandLineFlagsForTesting(input_file_);
    AddCode(input_file_, code);
    return internal::RunClangTool(command_line, file_contents_,
                                  std::move(action));
  }

 private:
  std::string input_file_ = "input.cc";
  absl::flat_hash_map<std::string, std::string> file_contents_;
};

// Flattens a vector of C++ code snippets into one line and removes consecutive
// runs of whitespace. This makes it easier to compare code snippets for
// testing. Note: This is not syntax-aware and will replace characters within
// strings as well.
std::vector<std::string> UglifyAll(const std::vector<std::string>& snippets);

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_FRONTEND_ACTION_TEST_UTIL_H_
