// Copyright 2025 Google LLC
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

#include "sandboxed_api/sandbox2/util/library_resolver.h"

#include <cstddef>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/util/elf_parser.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/status_macros.h"

namespace sandbox2 {
namespace {

namespace cpu = ::sapi::cpu;
namespace file_util = ::sapi::file_util;
namespace host_cpu = ::sapi::host_cpu;

void LogContainer(const std::vector<std::string>& container) {
  for (size_t i = 0; i < container.size(); ++i) {
    SAPI_RAW_LOG(INFO, "[%4zd]=%s", i, container[i].c_str());
  }
}

absl::StatusOr<std::string> ExistingPathInsideDir(
    absl::string_view dir_path, absl::string_view relative_path) {
  auto path =
      sapi::file::CleanPath(sapi::file::JoinPath(dir_path, relative_path));
  if (file_util::fileops::StripBasename(path) != dir_path) {
    return absl::InvalidArgumentError("Relative path goes above the base dir");
  }
  if (!file_util::fileops::Exists(path, false)) {
    return absl::NotFoundError(absl::StrCat("Does not exist: ", path));
  }
  return path;
}

absl::Status ValidateInterpreter(absl::string_view interpreter) {
  const absl::flat_hash_set<std::string> allowed_interpreters = {
      "/lib64/ld-linux-x86-64.so.2",
      "/lib64/ld64.so.2",            // PPC64
      "/lib/ld-linux-aarch64.so.1",  // AArch64
      "/lib/ld-linux-armhf.so.3",    // Arm
  };

  if (!allowed_interpreters.contains(interpreter)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Interpreter not on the whitelist: ", interpreter));
  }
  return absl::OkStatus();
}

std::string ResolveLibraryPath(absl::string_view lib_name,
                               const std::vector<std::string>& search_paths) {
  for (const auto& search_path : search_paths) {
    if (auto path_or = ExistingPathInsideDir(search_path, lib_name);
        path_or.ok()) {
      return path_or.value();
    }
  }
  return "";
}

constexpr absl::string_view GetPlatformCPUName() {
  switch (host_cpu::Architecture()) {
    case cpu::kX8664:
      return "x86_64";
    case cpu::kPPC64LE:
      return "ppc64";
    case cpu::kArm64:
      return "aarch64";
    default:
      return "unknown";
  }
}

std::string GetPlatform(absl::string_view interpreter) {
  return absl::StrCat(GetPlatformCPUName(), "-linux-gnu");
}

}  // namespace

absl::StatusOr<std::string> ResolveLibraryPaths(
    const std::string& path, absl::string_view ld_library_path,
    absl::FunctionRef<void(absl::string_view)> callback) {
  SAPI_ASSIGN_OR_RETURN(std::unique_ptr<ElfParser> file,
                        ElfParser::Create(path, false));
  return ResolveLibraryPaths(
      *file, false, ld_library_path,
      [&](absl::string_view lib, std::unique_ptr<ElfParser>&& parser) {
        callback(lib);
      });
}

absl::StatusOr<std::string> ResolveLibraryPaths(
    ElfParser& file, bool mmap_libs, absl::string_view ld_library_path,
    absl::FunctionRef<void(absl::string_view, std::unique_ptr<ElfParser>&&)>
        callback) {
  SAPI_ASSIGN_OR_RETURN(std::string interpreter, file.ReadInterpreter());
  if (interpreter.empty()) {
    SAPI_RAW_VLOG(1, "The file %s is not a dynamic executable",
                  file.filename().c_str());
    return interpreter;
  }

  SAPI_RAW_VLOG(1, "The file %s is using interpreter %s",
                file.filename().c_str(), interpreter.c_str());
  SAPI_RETURN_IF_ERROR(ValidateInterpreter(interpreter));

  std::vector<std::string> search_paths;
  // 1. LD_LIBRARY_PATH
  if (!ld_library_path.empty()) {
    std::vector<std::string> ld_library_paths =
        absl::StrSplit(ld_library_path, absl::ByAnyChar(":;"));
    search_paths.insert(search_paths.end(), ld_library_paths.begin(),
                        ld_library_paths.end());
  }
  // 2. Standard paths
  search_paths.insert(search_paths.end(), {
                                              "/lib",
                                              "/lib64",
                                              "/usr/lib",
                                              "/usr/lib64",
                                          });
  std::vector<std::string> hw_cap_paths = {
      GetPlatform(interpreter),
      "tls",
  };
  std::vector<std::string> full_search_paths;
  for (const auto& search_path : search_paths) {
    for (int hw_caps_set = (1 << hw_cap_paths.size()) - 1; hw_caps_set >= 0;
         --hw_caps_set) {
      std::string path = search_path;
      for (int hw_cap = 0; hw_cap < hw_cap_paths.size(); ++hw_cap) {
        if ((hw_caps_set & (1 << hw_cap)) != 0) {
          path = sapi::file::JoinPath(path, hw_cap_paths[hw_cap]);
        }
      }
      if (file_util::fileops::Exists(path, /*fully_resolve=*/false)) {
        full_search_paths.push_back(path);
      }
    }
  }

  // Arbitrary cut-off values, so we can safely resolve the libs.
  constexpr int kMaxWorkQueueSize = 1000;
  constexpr int kMaxResolvingDepth = 10;
  constexpr int kMaxResolvedEntries = 1000;
  constexpr int kMaxLoadedEntries = 100;
  constexpr int kMaxImportedLibraries = 100;

  absl::flat_hash_set<std::string> imported_libraries;
  std::vector<std::pair<std::string, int>> to_resolve;
  {
    SAPI_ASSIGN_OR_RETURN(auto imported_libs, file.ReadImportedLibraries());
    if (imported_libs.size() > kMaxWorkQueueSize) {
      return absl::FailedPreconditionError(
          "Exceeded max entries pending resolving limit");
    }
    for (const auto& imported_lib : imported_libs) {
      to_resolve.emplace_back(imported_lib, 1);
    }

    if (SAPI_RAW_VLOG_IS_ON(1)) {
      SAPI_RAW_VLOG(
          1, "Resolving dynamic library dependencies of %s using these dirs:",
          file.filename().c_str());
      LogContainer(full_search_paths);
    }
    if (SAPI_RAW_VLOG_IS_ON(2)) {
      SAPI_RAW_VLOG(
          2, "Direct dependencies of %s to resolve:", file.filename().c_str());
      LogContainer(imported_libs);
    }
  }

  // This is DFS with an auxiliary stack
  int resolved = 0;
  int loaded = 0;
  while (!to_resolve.empty()) {
    int depth;
    std::string lib;
    std::tie(lib, depth) = to_resolve.back();
    to_resolve.pop_back();
    ++resolved;
    if (resolved > kMaxResolvedEntries) {
      return absl::FailedPreconditionError(
          "Exceeded max resolved entries limit");
    }
    if (depth > kMaxResolvingDepth) {
      return absl::FailedPreconditionError(
          "Exceeded max resolving depth limit");
    }
    std::string resolved_lib = ResolveLibraryPath(lib, full_search_paths);
    if (resolved_lib.empty()) {
      SAPI_RAW_LOG(ERROR, "Failed to resolve library: %s", lib.c_str());
      continue;
    }
    if (imported_libraries.contains(resolved_lib)) {
      continue;
    }

    SAPI_RAW_VLOG(1, "Resolved library: %s => %s", lib.c_str(),
                  resolved_lib.c_str());

    imported_libraries.insert(resolved_lib);
    if (imported_libraries.size() > kMaxImportedLibraries) {
      return absl::FailedPreconditionError(
          "Exceeded max imported libraries limit");
    }
    ++loaded;
    if (loaded > kMaxLoadedEntries) {
      return absl::FailedPreconditionError("Exceeded max loaded entries limit");
    }
    SAPI_ASSIGN_OR_RETURN(std::unique_ptr<ElfParser> lib_elf,
                          ElfParser::Create(resolved_lib, mmap_libs));
    SAPI_ASSIGN_OR_RETURN(auto imported_libs, lib_elf->ReadImportedLibraries());
    if (imported_libs.size() > kMaxWorkQueueSize - to_resolve.size()) {
      return absl::FailedPreconditionError(
          "Exceeded max entries pending resolving limit");
    }

    if (SAPI_RAW_VLOG_IS_ON(2)) {
      SAPI_RAW_VLOG(2,
                    "Transitive dependencies of %s to resolve (depth = %d): ",
                    resolved_lib.c_str(), depth + 1);
      LogContainer(imported_libs);
    }

    for (const auto& imported_lib : imported_libs) {
      to_resolve.emplace_back(imported_lib, depth + 1);
    }
    callback(resolved_lib, std::move(lib_elf));
  }

  return std::move(interpreter);
}

}  // namespace sandbox2
