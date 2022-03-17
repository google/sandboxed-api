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
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_replace.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/VirtualFileSystem.h"

namespace sapi {
namespace internal {

absl::Status RunClangTool(
    const std::vector<std::string> command_line,
    const absl::flat_hash_map<std::string, std::string> file_contents,
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

#if LLVM_VERSION_MAJOR >= 10
  clang::tooling::ToolInvocation invocation(command_line, std::move(action),
                                            files.get());
#else
  clang::tooling::ToolInvocation invocation(command_line, action.get(),
                                            files.get());
#endif
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
