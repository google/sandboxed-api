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

#include "sandboxed_api/tools/clang_generator/frontend_action_test_util.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/file_helpers.h"
#include "sandboxed_api/util/path.h"

namespace sapi {
namespace internal {

std::string GetTestFileContents(absl::string_view file) {
  std::string contents;
  CHECK_OK(file::GetContents(GetTestSourcePath(file::JoinPath(
                                 "tools/clang_generator/testdata/", file)),
                             &contents, file::Defaults()));
  return contents;
}

absl::Status RunClangTool(
    const std::vector<std::string>& command_line,
    const absl::flat_hash_map<std::string, std::string>& file_contents,
    std::unique_ptr<clang::FrontendAction> action) {
  // Setup an in-memory virtual filesystem
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs(
      new llvm::vfs::InMemoryFileSystem());
  llvm::IntrusiveRefCntPtr<clang::FileManager> files =
      new clang::FileManager(clang::FileSystemOptions(), fs);

  for (const auto& [filename, content] : file_contents) {
    if (!fs->addFile(filename, /*ModificationTime=*/0,
                     llvm::MemoryBuffer::getMemBuffer(content))) {
      return absl::UnknownError(
          absl::StrCat("Couldn't add file to in-memory VFS: ", filename));
    }
  }

  clang::tooling::ToolInvocation invocation(command_line, std::move(action),
                                            files.get());
  if (!invocation.run()) {
    return absl::UnknownError("Tool invocation failed");
  }
  return absl::OkStatus();
}

}  // namespace internal

std::vector<std::string> FrontendActionTest::GetCommandLineFlagsForTesting(
    absl::string_view input_file) {
  return {"tool", "-fsyntax-only", "--std=c++17",
          "-I.",  "-Wno-error",    std::string(input_file)};
}

// Replaces all newlines with spaces and removes consecutive runs of whitespace.
std::string Uglify(absl::string_view code) {
  std::string result = absl::StrReplaceAll(code, {{"\n", " "}});
  absl::RemoveExtraAsciiWhitespace(&result);
  return result;
}

std::vector<std::string> UglifyAll(const std::vector<std::string>& snippets) {
  std::vector<std::string> result;
  result.reserve(snippets.size());
  std::transform(snippets.cbegin(), snippets.cend(), std::back_inserter(result),
                 Uglify);
  return result;
}

}  // namespace sapi
