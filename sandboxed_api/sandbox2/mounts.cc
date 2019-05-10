// Copyright 2019 Google LLC. All Rights Reserved.
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

#include "sandboxed_api/sandbox2/mounts.h"

#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>

#include <climits>
#include <memory>
#include <utility>

#include "google/protobuf/util/message_differencer.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/sandbox2/util/minielf.h"
#include "sandboxed_api/sandbox2/util/path.h"
#include "sandboxed_api/sandbox2/util/strerror.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/canonical_errors.h"
#include "sandboxed_api/util/status_macros.h"
#include "sandboxed_api/util/statusor.h"

namespace sandbox2 {
namespace {

bool PathContainsNullByte(absl::string_view path) {
  return path.find('\x00') != absl::string_view::npos;
}

bool IsSameFile(const std::string& path1, const std::string& path2) {
  struct stat stat1, stat2;
  if (stat(path1.c_str(), &stat1) == -1) {
    return false;
  }

  if (stat(path2.c_str(), &stat2) == -1) {
    return false;
  }

  return stat1.st_dev == stat2.st_dev && stat1.st_ino == stat2.st_ino;
}

bool IsEquivalentNode(const sandbox2::MountTree::Node& n1,
                      const sandbox2::MountTree::Node& n2) {
  // Node equals 1:1
  if (google::protobuf::util::MessageDifferencer::Equals(n1, n2)) {
    return true;
  }

  if (n1.node_case() != n2.node_case()) {
    return false;
  }

  // Check whether files/dirs are the same (e.g symlinks / hardlinks)
  switch (n1.node_case()) {
    case sandbox2::MountTree::Node::kFileNode:
      return n1.file_node().is_ro() == n2.file_node().is_ro() &&
             IsSameFile(n1.file_node().outside(), n2.file_node().outside());
    case sandbox2::MountTree::Node::kDirNode:
      return n1.dir_node().is_ro() == n2.dir_node().is_ro() &&
             IsSameFile(n1.dir_node().outside(), n2.dir_node().outside());
    default:
      return false;
  }
}

absl::string_view GetOutsidePath(const MountTree::Node& node) {
  switch (node.node_case()) {
    case MountTree::Node::kFileNode:
      return node.file_node().outside();
    case MountTree::Node::kDirNode:
      return node.dir_node().outside();
    default:
      SAPI_RAW_LOG(FATAL, "Invalid node type");
      return "";  // NOT REACHED
  }
}

::sapi::StatusOr<std::string> ExistingPathInsideDir(
    absl::string_view dir_path, absl::string_view relative_path) {
  auto path = file::CleanPath(file::JoinPath(dir_path, relative_path));
  if (file_util::fileops::StripBasename(path) != dir_path) {
    return ::sapi::InvalidArgumentError(
        "Relative path goes above the base dir");
  }
  if (!file_util::fileops::Exists(path, false)) {
    return ::sapi::NotFoundError(absl::StrCat("Does not exist: ", path));
  }
  return path;
}

sapi::Status ValidateInterpreter(absl::string_view interpreter) {
  const absl::flat_hash_set<std::string> allowed_interpreters = {
      "/lib64/ld-linux-x86-64.so.2",
  };

  if (!allowed_interpreters.contains(interpreter)) {
    return ::sapi::InvalidArgumentError(
        absl::StrCat("Interpreter not on the whitelist: ", interpreter));
  }
  return ::sapi::OkStatus();
}

std::string ResolveLibraryPath(absl::string_view lib_name,
                               const std::vector<std::string>& search_paths) {
  for (const auto& search_path : search_paths) {
    auto path_or = ExistingPathInsideDir(search_path, lib_name);
    if (path_or.ok()) {
      return path_or.ValueOrDie();
    }
  }
  return "";
}

std::string GetPlatform(absl::string_view interpreter) {
#if defined(__x86_64__)
  constexpr absl::string_view kCpuPlatform = "x86_64";
#elif defined(__powerpc64__)
  constexpr absl::string_view kCpuPlatform = "ppc64";
#else
  constexpr absl::string_view kCpuPlatform = "unknown";
#endif
  return absl::StrCat(kCpuPlatform, "-linux-gnu");
}

}  // namespace

::sapi::Status Mounts::Insert(absl::string_view path,
                              const MountTree::Node& new_node) {
  // Some sandboxes allow the inside/outside paths to be partially
  // user-controlled with some sanitization.
  // Since we're handling C++ strings and later convert them to C style
  // strings, a null byte in a path component might silently truncate the path
  // and mount something not expected by the caller. Check for null bytes in the
  // strings to protect against this.
  if (PathContainsNullByte(path)) {
    return ::sapi::InvalidArgumentError(
        absl::StrCat("Inside path contains a null byte: ", path));
  }
  switch (new_node.node_case()) {
    case MountTree::Node::kFileNode:
    case MountTree::Node::kDirNode: {
      auto outside_path = GetOutsidePath(new_node);
      if (outside_path.empty()) {
        return ::sapi::InvalidArgumentError("Outside path cannot be empty");
      }
      if (PathContainsNullByte(outside_path)) {
        return ::sapi::InvalidArgumentError(
            absl::StrCat("Outside path contains a null byte: ", outside_path));
      }
      break;
    }
    case MountTree::Node::kTmpfsNode:
    case MountTree::Node::NODE_NOT_SET:
      break;
  }

  std::string fixed_path = file::CleanPath(path);

  if (!absl::StartsWith(fixed_path, "/")) {
    return ::sapi::InvalidArgumentError("Only absolute paths are supported");
  }

  if (fixed_path == "/") {
    return ::sapi::InvalidArgumentError("The root already exists");
  }

  std::vector<absl::string_view> parts;

  auto split = file::SplitPath(fixed_path);
  absl::string_view cur = split.first;
  auto final_part = std::string(split.second);

  while (cur != "/") {
    auto split = file::SplitPath(cur);
    cur = split.first;
    parts.push_back(split.second);
  }

  MountTree* curtree = &mount_tree_;
  for (auto part = parts.rbegin(); part != parts.rend(); ++part) {
    curtree = &(curtree->mutable_entries()
                    ->insert({std::string(*part), MountTree()})
                    .first->second);
    if (curtree->has_node() && curtree->node().has_file_node()) {
      return ::sapi::FailedPreconditionError(
          absl::StrCat("Cannot insert ", path,
                       " since a file is mounted as a parent directory"));
    }
  }

  curtree = &(curtree->mutable_entries()
                  ->insert({final_part, MountTree()})
                  .first->second);

  if (curtree->has_node()) {
    if (IsEquivalentNode(curtree->node(), new_node)) {
      SAPI_RAW_LOG(INFO, "Inserting %s with the same value twice", path);
      return ::sapi::OkStatus();
    }
    return ::sapi::FailedPreconditionError(absl::StrCat(
        "Inserting ", path, " twice with conflicting values ",
        curtree->node().DebugString(), " vs. ", new_node.DebugString()));
  }

  if (new_node.has_file_node() && !curtree->entries().empty()) {
    return ::sapi::FailedPreconditionError(
        absl::StrCat("Trying to mount file over existing directory at ", path));
  }

  *curtree->mutable_node() = new_node;
  return ::sapi::OkStatus();
}

::sapi::Status Mounts::AddFile(absl::string_view path, bool is_ro) {
  return AddFileAt(path, path, is_ro);
}

::sapi::Status Mounts::AddFileAt(absl::string_view outside,
                                 absl::string_view inside, bool is_ro) {
  MountTree::Node node;
  auto* file_node = node.mutable_file_node();
  file_node->set_outside(std::string(outside));
  file_node->set_is_ro(is_ro);
  return Insert(inside, node);
}

::sapi::Status Mounts::AddDirectoryAt(absl::string_view outside,
                                      absl::string_view inside, bool is_ro) {
  MountTree::Node node;
  auto dir_node = node.mutable_dir_node();
  dir_node->set_outside(std::string(outside));
  dir_node->set_is_ro(is_ro);
  return Insert(inside, node);
}

namespace {

void LogContainer(const std::vector<std::string>& container) {
  for (size_t i = 0; i < container.size(); ++i) {
    SAPI_RAW_LOG(INFO, "[%4d]=%s", i, container[i]);
  }
}

}  // namespace

::sapi::Status Mounts::AddMappingsForBinary(const std::string& path,
                                            absl::string_view ld_library_path) {
  auto elf_or = ElfFile::ParseFromFile(
      path, ElfFile::kGetInterpreter | ElfFile::kLoadImportedLibraries);
  if (!elf_or.ok()) {
    return ::sapi::FailedPreconditionError(
        absl::StrCat("Could not parse ELF file: ", elf_or.status().message()));
  }
  auto elf = elf_or.ValueOrDie();
  const std::string interpreter = elf.interpreter();

  if (interpreter.empty()) {
    SAPI_RAW_VLOG(1, "The file %s is not a dynamic executable", path);
    return ::sapi::OkStatus();
  }

  SAPI_RAW_VLOG(1, "The file %s is using interpreter %s", path, interpreter);
  SAPI_RETURN_IF_ERROR(ValidateInterpreter(interpreter));

  std::vector<std::string> search_paths;
  // 1. LD_LIBRARY_PRELOAD
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
          path = file::JoinPath(path, hw_cap_paths[hw_cap]);
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
    auto imported_libs = elf.imported_libraries();
    if (imported_libs.size() > kMaxWorkQueueSize) {
      return ::sapi::FailedPreconditionError(
          "Exceeded max entries pending resolving limit");
    }
    for (const auto& imported_lib : imported_libs) {
      to_resolve.emplace_back(imported_lib, 1);
    }

    if (SAPI_VLOG_IS_ON(1)) {
      SAPI_RAW_VLOG(
          1, "Resolving dynamic library dependencies of %s using these dirs:",
          path);
      LogContainer(full_search_paths);
    }
    if (SAPI_VLOG_IS_ON(2)) {
      SAPI_RAW_VLOG(2, "Direct dependencies of %s to resolve:", path);
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
      return ::sapi::FailedPreconditionError(
          "Exceeded max resolved entries limit");
    }
    if (depth > kMaxResolvingDepth) {
      return ::sapi::FailedPreconditionError(
          "Exceeded max resolving depth limit");
    }
    std::string resolved_lib = ResolveLibraryPath(lib, full_search_paths);
    if (resolved_lib.empty()) {
      SAPI_RAW_LOG(ERROR, "Failed to resolve library: %s", lib);
      continue;
    }
    if (imported_libraries.contains(resolved_lib)) {
      continue;
    }

    SAPI_RAW_VLOG(1, "Resolved library: %s ==> %s", lib, resolved_lib);

    imported_libraries.insert(resolved_lib);
    if (imported_libraries.size() > kMaxImportedLibraries) {
      return ::sapi::FailedPreconditionError(
          "Exceeded max imported libraries limit");
    }
    ++loaded;
    if (loaded > kMaxLoadedEntries) {
      return ::sapi::FailedPreconditionError(
          "Exceeded max loaded entries limit");
    }
    SAPI_ASSIGN_OR_RETURN(
        auto lib_elf,
        ElfFile::ParseFromFile(resolved_lib, ElfFile::kLoadImportedLibraries));
    auto imported_libs = lib_elf.imported_libraries();
    if (imported_libs.size() > kMaxWorkQueueSize - to_resolve.size()) {
      return ::sapi::FailedPreconditionError(
          "Exceeded max entries pending resolving limit");
    }

    if (SAPI_VLOG_IS_ON(2)) {
      SAPI_RAW_VLOG(2,
                    "Transitive dependencies of %s to resolve (depth = %d): ",
                    resolved_lib, depth + 1);
      LogContainer(imported_libs);
    }

    for (const auto& imported_lib : imported_libs) {
      to_resolve.emplace_back(imported_lib, depth + 1);
    }
  }

  SAPI_RETURN_IF_ERROR(AddFile(interpreter));
  for (const auto& lib : imported_libraries) {
    SAPI_RETURN_IF_ERROR(AddFile(lib));
  }

  return ::sapi::OkStatus();
}

::sapi::Status Mounts::AddTmpfs(absl::string_view inside, size_t sz) {
  MountTree::Node node;
  auto tmpfs_node = node.mutable_tmpfs_node();
  tmpfs_node->set_tmpfs_options(absl::StrCat("size=", sz));
  return Insert(inside, node);
}

namespace {

uint64_t GetMountFlagsFor(const std::string& path) {
  struct statvfs vfs;
  if (TEMP_FAILURE_RETRY(statvfs(path.c_str(), &vfs)) == -1) {
    SAPI_RAW_PLOG(ERROR, "statvfs");
    return 0;
  }

  static constexpr struct {
    const uint64_t mount_flag;
    const uint64_t vfs_flag;
  } mount_pairs[] = {
      {MS_NOSUID, ST_NOSUID},         {MS_NODEV, ST_NODEV},
      {MS_NOEXEC, ST_NOEXEC},         {MS_SYNCHRONOUS, ST_SYNCHRONOUS},
      {MS_MANDLOCK, ST_MANDLOCK},     {MS_NOATIME, ST_NOATIME},
      {MS_NODIRATIME, ST_NODIRATIME}, {MS_RELATIME, ST_RELATIME},
  };

  uint64_t flags = 0;
  for (const auto& i : mount_pairs) {
    if (vfs.f_flag & i.vfs_flag) {
      flags |= i.mount_flag;
    }
  }

  return flags;
}

void MountWithDefaults(const std::string& source, const std::string& target,
                       const char* fs_type, uint64_t extra_flags,
                       const char* option_str, bool is_ro) {
  uint64_t flags = MS_REC | MS_NODEV | MS_NOSUID | extra_flags;
  SAPI_RAW_VLOG(1, R"(mount("%s", "%s", "%s", %d, "%s"))", source, target,
                fs_type, flags, option_str);

  int res = mount(source.c_str(), target.c_str(), fs_type, flags, option_str);
  if (res == -1) {
    if (errno == ENOENT) {
      // File does not exist (anymore). This is e.g. the case when we're trying
      // to gather stack-traces on SAPI crashes. The sandboxee application is a
      // memfd file that is not existing anymore.
      SAPI_RAW_LOG(WARNING, "Could not mount %s: file does not exist", source);
      return;
    }
    SAPI_RAW_PLOG(FATAL, "mounting %s to %s failed", source, target);
  }

  // Bind mounting as read-only doesn't work, we have to remount it.
  if (is_ro) {
    // Get actual mount flags.
    flags |= GetMountFlagsFor(target);
    res = mount("", target.c_str(), "", flags | MS_REMOUNT | MS_RDONLY,
                option_str);
    SAPI_RAW_PCHECK(res != -1, "remounting %s read-only failed", target);
  }
}

// Traverses the MountTree to create all required files and perform the mounts.
void CreateMounts(const MountTree& tree, const std::string& path,
                  bool create_backing_files) {
  // First, create the backing files if needed.
  if (create_backing_files) {
    switch (tree.node().node_case()) {
      case MountTree::Node::kFileNode: {
        SAPI_RAW_VLOG(2, "Creating backing file at %s", path);
        int fd = open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
        SAPI_RAW_PCHECK(fd != -1, "");
        SAPI_RAW_PCHECK(close(fd) == 0, "");
        break;
      }
      case MountTree::Node::kDirNode:
      case MountTree::Node::kTmpfsNode:
      case MountTree::Node::NODE_NOT_SET:
        SAPI_RAW_VLOG(2, "Creating directory at %s", path);
        SAPI_RAW_PCHECK(mkdir(path.c_str(), 0700) == 0 || errno == EEXIST, "");
        break;
        // Intentionally no default to make sure we handle all the cases.
    }
  }

  // Perform the actual mounts based on the node type.
  switch (tree.node().node_case()) {
    case MountTree::Node::kDirNode: {
      // Since this directory is bind mounted, it's the users
      // responsibility to make sure that all backing files are in place.
      create_backing_files = false;

      auto node = tree.node().dir_node();
      MountWithDefaults(node.outside(), path, "", MS_BIND, nullptr,
                        node.is_ro());
      break;
    }
    case MountTree::Node::kTmpfsNode: {
      // We can always create backing files under a tmpfs.
      create_backing_files = true;

      auto node = tree.node().tmpfs_node();
      MountWithDefaults("", path, "tmpfs", 0, node.tmpfs_options().c_str(),
                        /* is_ro */ false);
      break;
    }
    case MountTree::Node::kFileNode: {
      auto node = tree.node().file_node();
      MountWithDefaults(node.outside(), path, "", MS_BIND, nullptr,
                        node.is_ro());

      // A file node has to be a leaf so we can skip traversing here.
      return;
    }
    case MountTree::Node::NODE_NOT_SET:
      // Nothing to do, we already created the directory above.
      break;
      // Intentionally no default to make sure we handle all the cases.
  }

  // Traverse the subtrees.
  for (const auto& kv : tree.entries()) {
    std::string new_path = file::JoinPath(path, kv.first);
    CreateMounts(kv.second, new_path, create_backing_files);
  }
}

}  // namespace

void Mounts::CreateMounts(const std::string& root_path) const {
  sandbox2::CreateMounts(mount_tree_, root_path, true);
}

namespace {

void RecursivelyListMountsImpl(const MountTree& tree,
                               const std::string& tree_path,
                               std::vector<std::string>* outside_entries,
                               std::vector<std::string>* inside_entries) {
  const MountTree::Node& node = tree.node();
  if (node.has_dir_node()) {
    const char* rw_str = node.dir_node().is_ro() ? "R " : "W ";
    inside_entries->emplace_back(absl::StrCat(rw_str, tree_path, "/"));
    outside_entries->emplace_back(absl::StrCat(node.dir_node().outside(), "/"));
  } else if (node.has_file_node()) {
    const char* rw_str = node.file_node().is_ro() ? "R " : "W ";
    inside_entries->emplace_back(absl::StrCat(rw_str, tree_path));
    outside_entries->emplace_back(absl::StrCat(node.file_node().outside()));
  } else if (node.has_tmpfs_node()) {
    outside_entries->emplace_back(
        absl::StrCat("tmpfs: ", node.tmpfs_node().tmpfs_options()));
  }

  for (const auto& subentry : tree.entries()) {
    RecursivelyListMountsImpl(subentry.second,
                              absl::StrCat(tree_path, "/", subentry.first),
                              outside_entries, inside_entries);
  }
}

}  // namespace

void Mounts::RecursivelyListMounts(std::vector<std::string>* outside_entries,
                                   std::vector<std::string>* inside_entries) {
  RecursivelyListMountsImpl(GetMountTree(), "", outside_entries,
                            inside_entries);
}

}  // namespace sandbox2
