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
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "clang/Frontend/FrontendAction.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sapi {
namespace internal {

absl::Status RunClangTool(
    const std::vector<std::string> command_line,
    const absl::flat_hash_map<std::string, std::string> file_contents,
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

  virtual std::vector<std::string> GetCommandLineFlagsForTesting(
      absl::string_view input_file);

  // Runs the specified frontend action on in-memory source code.
  absl::Status RunFrontendAction(
      absl::string_view code, std::unique_ptr<clang::FrontendAction> action) {
    std::vector<std::string> command_line =
        GetCommandLineFlagsForTesting(input_file_);
    AddCode(input_file_, code);
    return internal::RunClangTool(command_line, file_contents_,
                                  std::move(action));
  }

  // Runs the specified frontend action. Provided for compatibility with LLVM <
  // 10. Takes ownership.
  absl::Status RunFrontendAction(absl::string_view code,
                                 clang::FrontendAction* action) {
    return RunFrontendAction(code, absl::WrapUnique(action));
  }

 private:
  std::string input_file_ = "input.cc";
  absl::flat_hash_map<std::string, std::string> file_contents_;
};

// Flattens a piece of C++ code into one line and removes consecutive runs of
// whitespace. This makes it easier to compare code snippets for testing.
// Note: This is not syntax-aware and will replace characters within strings as
// well.
std::string Uglify(absl::string_view code);

std::vector<std::string> UglifyAll(const std::vector<std::string>& snippets);

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_FRONTEND_ACTION_TEST_UTIL_H_
