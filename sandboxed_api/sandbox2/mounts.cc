// Copyright 2019 Google LLC
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

#include "sandboxed_api/sandbox2/mounts.h"

#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "sandboxed_api/sandbox2/mount_tree.pb.h"
#include "sandboxed_api/sandbox2/util/library_resolver.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/status_macros.h"

namespace sandbox2 {
namespace {

namespace file_util = ::sapi::file_util;

bool PathContainsNullByte(absl::string_view path) {
  return absl::StrContains(path, '\0');
}

absl::string_view GetOutsidePath(const MountTree::Node& node) {
  switch (node.node_case()) {
    case MountTree::Node::kFileNode:
      return node.file_node().outside();
    case MountTree::Node::kDirNode:
      return node.dir_node().outside();
    default:
      SAPI_RAW_LOG(FATAL, "Invalid node type");
  }
}

}  // namespace

namespace internal {

bool IsSameFile(const std::string& path1, const std::string& path2) {
  if (path1 == path2) {
    return true;
  }

  struct stat stat1, stat2;
  if (stat(path1.c_str(), &stat1) == -1) {
    return false;
  }

  if (stat(path2.c_str(), &stat2) == -1) {
    return false;
  }

  return stat1.st_dev == stat2.st_dev && stat1.st_ino == stat2.st_ino;
}

bool IsWritable(const MountTree::Node& node) {
  switch (node.node_case()) {
    case MountTree::Node::kFileNode:
      return node.file_node().writable();
    case MountTree::Node::kDirNode:
      return node.dir_node().writable();
    case MountTree::Node::kRootNode:
      return node.root_node().writable();
    default:
      return false;
  }
}

bool HasSameTarget(const MountTree::Node& n1, const MountTree::Node& n2) {
  // Return early when node types are different
  if (n1.node_case() != n2.node_case()) {
    return false;
  }
  // Compare proto fields
  switch (n1.node_case()) {
    case MountTree::Node::kFileNode:
      // Check whether files are the same (e.g. symlinks / hardlinks)
      return IsSameFile(n1.file_node().outside(), n2.file_node().outside());
    case MountTree::Node::kDirNode:
      // Check whether dirs are the same (e.g. symlinks / hardlinks)
      return IsSameFile(n1.dir_node().outside(), n2.dir_node().outside());
    case MountTree::Node::kTmpfsNode:
      return n1.tmpfs_node().tmpfs_options() == n2.tmpfs_node().tmpfs_options();
    case MountTree::Node::kRootNode:
      return true;
    default:
      return false;
  }
}

bool IsEquivalentNode(const MountTree::Node& n1, const MountTree::Node& n2) {
  if (!HasSameTarget(n1, n2)) {
    return false;
  }

  // Compare proto fields
  switch (n1.node_case()) {
    case MountTree::Node::kFileNode:
      return n1.file_node().writable() == n2.file_node().writable();
    case MountTree::Node::kDirNode:
      return n1.dir_node().writable() == n2.dir_node().writable();
    case MountTree::Node::kTmpfsNode:
      return true;
    case MountTree::Node::kRootNode:
      return n1.root_node().writable() == n2.root_node().writable();
    default:
      return false;
  }
}

}  // namespace internal

absl::Status Mounts::Remove(absl::string_view path) {
  if (PathContainsNullByte(path)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Path contains a null byte: ", path));
  }

  std::string fixed_path = sapi::file::CleanPath(path);
  if (!sapi::file::IsAbsolutePath(fixed_path)) {
    return absl::InvalidArgumentError("Only absolute paths are supported");
  }

  if (fixed_path == "/") {
    return absl::InvalidArgumentError("Cannot remove root");
  }
  std::vector<absl::string_view> parts =
      absl::StrSplit(absl::StripPrefix(fixed_path, "/"), '/');

  MountTree* curtree = &mount_tree_;
  for (absl::string_view part : parts) {
    if (curtree->has_node() && curtree->node().has_file_node()) {
      return absl::NotFoundError(
          absl::StrCat("File node is mounted at parent of: ", path));
    }
    auto it = curtree->mutable_entries()->find(std::string(part));
    if (it == curtree->mutable_entries()->end()) {
      return absl::NotFoundError(
          absl::StrCat("Path does not exist in mounts: ", path));
    }
    curtree = &it->second;
  }
  curtree->clear_node();
  curtree->clear_entries();
  return absl::OkStatus();
}

absl::StatusOr<MountTree::Node> Mounts::GetNode(absl::string_view path) {
  std::vector<absl::string_view> parts =
      absl::StrSplit(absl::StripPrefix(path, "/"), '/');
  MountTree* curtree = &mount_tree_;
  for (absl::string_view part : parts) {
    auto it = curtree->mutable_entries()->find(std::string(part));
    if (it == curtree->mutable_entries()->end()) {
      return absl::NotFoundError(
          absl::StrCat("Path does not exist in mounts: ", path));
    }
    curtree = &it->second;
  }
  return curtree->node();
}

absl::Status Mounts::Insert(absl::string_view path,
                            const MountTree::Node& new_node) {
  // Some sandboxes allow the inside/outside paths to be partially
  // user-controlled with some sanitization.
  // Since we're handling C++ strings and later convert them to C style
  // strings, a null byte in a path component might silently truncate the path
  // and mount something not expected by the caller. Check for null bytes in the
  // strings to protect against this.
  if (PathContainsNullByte(path)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Inside path contains a null byte: ", path));
  }
  switch (new_node.node_case()) {
    case MountTree::Node::kFileNode:
    case MountTree::Node::kDirNode: {
      auto outside_path = GetOutsidePath(new_node);
      if (outside_path.empty()) {
        return absl::InvalidArgumentError("Outside path cannot be empty");
      }
      if (PathContainsNullByte(outside_path)) {
        return absl::InvalidArgumentError(
            absl::StrCat("Outside path contains a null byte: ", outside_path));
      }
      break;
    }
    case MountTree::Node::kRootNode:
      return absl::InvalidArgumentError("Cannot insert a RootNode");
    case MountTree::Node::kTmpfsNode:
    case MountTree::Node::NODE_NOT_SET:
      break;
  }

  std::string fixed_path = sapi::file::CleanPath(path);
  if (!sapi::file::IsAbsolutePath(fixed_path)) {
    return absl::InvalidArgumentError("Only absolute paths are supported");
  }

  if (fixed_path == "/") {
    return absl::InvalidArgumentError("The root already exists");
  }

  std::vector<absl::string_view> parts =
      absl::StrSplit(absl::StripPrefix(fixed_path, "/"), '/');

  MountTree* curtree = &mount_tree_;
  for (int i = 0; true; ++i) {
    auto [it, did_insert] =
        curtree->mutable_entries()->emplace(parts[i], MountTree());
    if (did_insert) {
      it->second.set_index(++mount_index_);
    }
    curtree = &it->second;
    if (i == parts.size() - 1) {  // Final part
      break;
    }
    if (curtree->has_node() && curtree->node().has_file_node()) {
      return absl::FailedPreconditionError(
          absl::StrCat("Cannot insert ", path,
                       " since a file is mounted as a parent directory"));
    }
  }

  if (curtree->has_node()) {
    if (internal::IsEquivalentNode(curtree->node(), new_node)) {
      SAPI_RAW_LOG(INFO, "Inserting %s with the same value twice",
                   std::string(path).c_str());
      return absl::OkStatus();
    }
    if (internal::HasSameTarget(curtree->node(), new_node)) {
      if (!internal::IsWritable(curtree->node()) &&
          internal::IsWritable(new_node)) {
        SAPI_RAW_LOG(INFO,
                     "Changing %s to writable, was inserted read-only before",
                     std::string(path).c_str());
        *curtree->mutable_node() = new_node;
        return absl::OkStatus();
      }
      if (internal::IsWritable(curtree->node()) &&
          !internal::IsWritable(new_node)) {
        SAPI_RAW_LOG(INFO,
                     "Inserting %s read-only is a nop, as it was inserted "
                     "writable before",
                     std::string(path).c_str());
        return absl::OkStatus();
      }
    }
    return absl::FailedPreconditionError(absl::StrCat(
        "Inserting ", path, " twice with conflicting values ",
        curtree->node().DebugString(), " vs. ", new_node.DebugString()));
  }

  if (new_node.has_file_node() && !curtree->entries().empty()) {
    return absl::FailedPreconditionError(
        absl::StrCat("Trying to mount file over existing directory at ", path));
  }

  *curtree->mutable_node() = new_node;
  return absl::OkStatus();
}

absl::Status Mounts::AddFileAt(absl::string_view outside,
                               absl::string_view inside, bool is_ro) {
  MountTree::Node node;
  auto* file_node = node.mutable_file_node();
  file_node->set_outside(std::string(outside));
  file_node->set_writable(!is_ro);
  return Insert(inside, node);
}

absl::Status Mounts::AddDirectoryAt(absl::string_view outside,
                                    absl::string_view inside, bool is_ro) {
  MountTree::Node node;
  auto* dir_node = node.mutable_dir_node();
  dir_node->set_outside(std::string(outside));
  dir_node->set_writable(!is_ro);
  return Insert(inside, node);
}

absl::StatusOr<std::string> Mounts::ResolvePath(absl::string_view path) const {
  if (!sapi::file::IsAbsolutePath(path)) {
    return absl::InvalidArgumentError("Path has to be absolute");
  }
  std::string fixed_path = sapi::file::CleanPath(path);
  absl::string_view tail = absl::StripPrefix(fixed_path, "/");

  const MountTree* curtree = &mount_tree_;
  while (!tail.empty()) {
    std::pair<absl::string_view, absl::string_view> parts =
        absl::StrSplit(tail, absl::MaxSplits('/', 1));
    const std::string cur(parts.first);
    const auto it = curtree->entries().find(cur);
    if (it == curtree->entries().end()) {
      if (curtree->node().has_dir_node()) {
        return sapi::file::JoinPath(curtree->node().dir_node().outside(), tail);
      }
      return absl::NotFoundError("Path could not be resolved in the mounts");
    }
    curtree = &it->second;
    tail = parts.second;
  }
  switch (curtree->node().node_case()) {
    case MountTree::Node::kFileNode:
    case MountTree::Node::kDirNode:
      return std::string(GetOutsidePath(curtree->node()));
    case MountTree::Node::kRootNode:
    case MountTree::Node::kTmpfsNode:
    case MountTree::Node::NODE_NOT_SET:
      break;
  }
  return absl::NotFoundError("Path could not be resolved in the mounts");
}

absl::Status Mounts::AddMappingsForBinary(const std::string& path,
                                          absl::string_view ld_library_path) {
  std::vector<std::string> imported_libraries;
  SAPI_ASSIGN_OR_RETURN(
      auto interpreter,
      ResolveLibraryPaths(path, ld_library_path, [&](absl::string_view lib) {
        imported_libraries.push_back(std::string(lib));
      }));
  if (!interpreter.empty()) {
    imported_libraries.push_back(interpreter);
  }
  for (const auto& lib : imported_libraries) {
    SAPI_RETURN_IF_ERROR(AddFile(lib));
  }

  return absl::OkStatus();
}

absl::Status Mounts::AddTmpfs(absl::string_view inside, size_t sz) {
  MountTree::Node node;
  auto tmpfs_node = node.mutable_tmpfs_node();
  tmpfs_node->set_tmpfs_options(absl::StrCat("size=", sz));
  return Insert(inside, node);
}

absl::Status Mounts::AllowMountPropagation(absl::string_view inside) {
  SAPI_ASSIGN_OR_RETURN(MountTree::Node node, GetNode(inside));
  if (!node.has_dir_node()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Path is not a directory: ", inside));
  }
  node.mutable_dir_node()->set_allow_mount_propagation(true);
  return absl::OkStatus();
}

namespace {

uint64_t GetMountFlagsFor(const std::string& path) {
  struct statvfs vfs;
  if (TEMP_FAILURE_RETRY(statvfs(path.c_str(), &vfs)) == -1) {
    SAPI_RAW_PLOG(ERROR, "statvfs");
    return 0;
  }

  uint64_t flags = 0;
  using MountPair = std::pair<uint64_t, uint64_t>;
  for (const auto& [mount_flag, vfs_flag] : {
           MountPair(MS_RDONLY, ST_RDONLY),
           MountPair(MS_NOSUID, ST_NOSUID),
           MountPair(MS_NODEV, ST_NODEV),
           MountPair(MS_NOEXEC, ST_NOEXEC),
           MountPair(MS_SYNCHRONOUS, ST_SYNCHRONOUS),
           MountPair(MS_MANDLOCK, ST_MANDLOCK),
           MountPair(MS_NOATIME, ST_NOATIME),
           MountPair(MS_NODIRATIME, ST_NODIRATIME),
           MountPair(MS_RELATIME, ST_RELATIME),
       }) {
    if (vfs.f_flag & vfs_flag) {
      flags |= mount_flag;
    }
  }
  return flags;
}

std::string MountFlagsToString(uint64_t flags) {
#define SAPI_MAP(x) {x, #x}
  static constexpr std::pair<uint64_t, absl::string_view> kMap[] = {
      SAPI_MAP(MS_RDONLY),      SAPI_MAP(MS_NOSUID),
      SAPI_MAP(MS_NODEV),       SAPI_MAP(MS_NOEXEC),
      SAPI_MAP(MS_SYNCHRONOUS), SAPI_MAP(MS_REMOUNT),
      SAPI_MAP(MS_MANDLOCK),    SAPI_MAP(MS_DIRSYNC),
      SAPI_MAP(MS_NOATIME),     SAPI_MAP(MS_NODIRATIME),
      SAPI_MAP(MS_BIND),        SAPI_MAP(MS_MOVE),
      SAPI_MAP(MS_REC),
#ifdef MS_VERBOSE
      SAPI_MAP(MS_VERBOSE),  // Deprecated
#endif
      SAPI_MAP(MS_SILENT),      SAPI_MAP(MS_POSIXACL),
      SAPI_MAP(MS_UNBINDABLE),  SAPI_MAP(MS_PRIVATE),
      SAPI_MAP(MS_SLAVE),  // Inclusive language: system constant
      SAPI_MAP(MS_SHARED),      SAPI_MAP(MS_RELATIME),
      SAPI_MAP(MS_KERNMOUNT),   SAPI_MAP(MS_I_VERSION),
      SAPI_MAP(MS_STRICTATIME),
#ifdef MS_LAZYTIME
      SAPI_MAP(MS_LAZYTIME),  // Added in Linux 4.0
#endif
  };
#undef SAPI_MAP
  std::vector<absl::string_view> flags_list;
  for (const auto& [val, str] : kMap) {
    if ((flags & val) == val) {
      flags &= ~val;
      flags_list.push_back(str);
    }
  }
  std::string flags_str = absl::StrCat(flags);
  if (flags_list.empty() || flags != 0) {
    flags_list.push_back(flags_str);
  }
  return absl::StrJoin(flags_list, "|");
}

void MountWithDefaults(const std::string& source, const std::string& target,
                       const char* fs_type, uint64_t extra_flags,
                       const char* option_str, bool is_ro) {
  uint64_t flags = MS_REC | MS_NOSUID | extra_flags;
  if (is_ro) {
    flags |= MS_RDONLY;
  }
  SAPI_RAW_VLOG(1, R"(mount("%s", "%s", "%s", %s, "%s"))", source.c_str(),
                target.c_str(), fs_type, MountFlagsToString(flags).c_str(),
                option_str);

  int res = mount(source.c_str(), target.c_str(), fs_type, flags, option_str);
  if (res == -1) {
    if (errno == ENOENT) {
      // File does not exist (anymore). This may be the case when trying to
      // gather stack-traces on SAPI crashes. The sandboxee application is a
      // memfd file that is not existing anymore.
      // Check which file/dir of the call is actually missing.
      bool have_source =
          file_util::fileops::Exists(source, /*fully_resolve=*/true);
      bool have_target =
          file_util::fileops::Exists(target, /*fully_resolve=*/true);
      const char* detail = "unknown error, source and target exist";
      if (!have_source && !have_target) {
        detail = "neither source nor target exist";
      } else if (!have_source) {
        detail = "source does not exist";
      } else if (!have_target) {
        detail = "target does not exist";
      }
      SAPI_RAW_LOG(WARNING, "Could not mount %s (source) to %s (target): %s",
                   source.c_str(), target.c_str(), detail);
      return;
    }
    SAPI_RAW_PLOG(FATAL, "mounting %s to %s failed (flags=%s)", source, target,
                  MountFlagsToString(flags));
  }

  // Flags are ignored for a bind mount, a remount is needed to set the flags.
  if (extra_flags & MS_BIND) {
    // Get actual mount flags.
    uint64_t target_flags = GetMountFlagsFor(target);
    if ((target_flags & MS_RDONLY) != 0 && (flags & MS_RDONLY) == 0) {
      SAPI_RAW_LOG(FATAL,
                   "cannot remount %s as read-write as it's on read-only dev",
                   target.c_str());
    }
    res = mount("", target.c_str(), "", flags | target_flags | MS_REMOUNT,
                nullptr);
    SAPI_RAW_PCHECK(res != -1, "remounting %s with flags=%s failed", target,
                    MountFlagsToString(flags));
  }

  // Mount propagation has to be set separately
  const uint64_t propagation =
      extra_flags & (MS_SHARED | MS_PRIVATE | MS_SLAVE | MS_UNBINDABLE);
  if (propagation != 0) {
    res = mount("", target.c_str(), "", propagation, nullptr);
    SAPI_RAW_PCHECK(res != -1, "changing %s mount propagation to %s failed",
                    target, MountFlagsToString(propagation).c_str());
  }
}

using MapEntry = std::pair<absl::string_view, const MountTree*>;

std::vector<MapEntry> GetSortedEntries(const MountTree& tree) {
  std::vector<MapEntry> ordered;
  ordered.reserve(tree.entries_size());
  for (auto& entry : tree.entries()) {
    ordered.emplace_back(entry.first, &entry.second);
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const MapEntry& a, const MapEntry& b) {
              return a.second->index() < b.second->index();
            });
  return ordered;
}

bool IsSymlink(const std::string& path) {
  struct stat sb;
  if (stat(path.c_str(), &sb) == -1) {
    return false;
  }
  return S_ISLNK(sb.st_mode);
}

// Traverses the MountTree to create all required files and perform the mounts.
void CreateMounts(const MountTree& tree, const std::string& root_path,
                  const std::string& path, bool create_backing_files,
                  bool allow_mount_propagation) {
  // First, create the backing files if needed.
  if (create_backing_files) {
    switch (tree.node().node_case()) {
      case MountTree::Node::kFileNode: {
        SAPI_RAW_VLOG(2, "Creating backing file at %s", path.c_str());
        int fd = open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
        SAPI_RAW_PCHECK(fd != -1, "");
        SAPI_RAW_PCHECK(close(fd) == 0, "");
        break;
      }
      case MountTree::Node::kDirNode:
      case MountTree::Node::kTmpfsNode:
      case MountTree::Node::kRootNode:
      case MountTree::Node::NODE_NOT_SET:
        SAPI_RAW_VLOG(2, "Creating directory at %s", path.c_str());
        SAPI_RAW_PCHECK(mkdir(path.c_str(), 0700) == 0 || errno == EEXIST, "");
        break;
        // Intentionally no default to make sure we handle all the cases.
    }
  }

  if (IsSymlink(path)) {
    std::string abs_path;
    if (!file_util::fileops::ReadLinkAbsolute(path, &abs_path)) {
      SAPI_RAW_LOG(WARNING, "could not resolve mount target path %s",
                   path.c_str());
    } else if (!absl::StartsWith(abs_path, absl::StrCat(root_path, "/"))) {
      SAPI_RAW_LOG(ERROR, "Mount target not within chroot: %s resolved to %s",
                   path.c_str(), abs_path.c_str());
    }
  }

  // Perform the actual mounts based on the node type.
  switch (tree.node().node_case()) {
    case MountTree::Node::kDirNode: {
      // Since this directory is bind mounted, it's the users
      // responsibility to make sure that all backing files are in place.
      create_backing_files = false;

      auto node = tree.node().dir_node();
      MountWithDefaults(
          node.outside(), path, "",
          MS_BIND | (node.allow_mount_propagation() || allow_mount_propagation
                         ? MS_SHARED
                         : MS_PRIVATE),
          nullptr, !node.writable());
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
                        !node.writable());

      // A file node has to be a leaf so we can skip traversing here.
      return;
    }
    case MountTree::Node::kRootNode:
    case MountTree::Node::NODE_NOT_SET:
      // Nothing to do, we already created the directory above.
      break;
      // Intentionally no default to make sure we handle all the cases.
  }

  // Traverse the subtrees.
  for (const auto& [key, value] : GetSortedEntries(tree)) {
    std::string new_path = sapi::file::JoinPath(path, key);
    CreateMounts(*value, root_path, new_path, create_backing_files,
                 allow_mount_propagation);
  }
}

}  // namespace

void Mounts::CreateMounts(const std::string& root_path,
                          bool allow_mount_propagation) const {
  sandbox2::CreateMounts(mount_tree_, root_path, root_path, true,
                         allow_mount_propagation);
}

namespace {

void RecursivelyListMountsImpl(const MountTree& tree,
                               const std::string& tree_path,
                               std::vector<std::string>* outside_entries,
                               std::vector<std::string>* inside_entries) {
  const MountTree::Node& node = tree.node();
  if (node.has_dir_node()) {
    const char* rw_str = node.dir_node().writable() ? "W " : "R ";
    inside_entries->emplace_back(absl::StrCat(rw_str, tree_path, "/"));
    outside_entries->emplace_back(absl::StrCat(node.dir_node().outside(), "/"));
  } else if (node.has_file_node()) {
    const char* rw_str = node.file_node().writable() ? "W " : "R ";
    inside_entries->emplace_back(absl::StrCat(rw_str, tree_path));
    outside_entries->emplace_back(absl::StrCat(node.file_node().outside()));
  } else if (node.has_tmpfs_node()) {
    inside_entries->emplace_back(tree_path);
    outside_entries->emplace_back(
        absl::StrCat("tmpfs: ", node.tmpfs_node().tmpfs_options()));
  }

  for (const auto& [key, value] : GetSortedEntries(tree)) {
    std::string new_path = sapi::file::JoinPath(tree_path, key);
    RecursivelyListMountsImpl(*value, absl::StrCat(tree_path, "/", key),
                              outside_entries, inside_entries);
  }
}

}  // namespace

void Mounts::RecursivelyListMounts(
    std::vector<std::string>* outside_entries,
    std::vector<std::string>* inside_entries) const {
  RecursivelyListMountsImpl(GetMountTree(), "", outside_entries,
                            inside_entries);
}

}  // namespace sandbox2
