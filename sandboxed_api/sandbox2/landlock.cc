// Copyright 2026 Google LLC
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

#include "sandboxed_api/sandbox2/landlock.h"

#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include "sandboxed_api/sandbox2/mount_tree.pb.h"
#include "sandboxed_api/sandbox2/mounts.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/raw_logging.h"

#ifndef LANDLOCK_RULE_PATH_BENEATH
#define LANDLOCK_RULE_PATH_BENEATH 1
#endif

// Define ABI v1 FS flags
#ifndef LANDLOCK_ACCESS_FS_EXECUTE
#define LANDLOCK_ACCESS_FS_EXECUTE (1ULL << 0)
#define LANDLOCK_ACCESS_FS_WRITE_FILE (1ULL << 1)
#define LANDLOCK_ACCESS_FS_READ_FILE (1ULL << 2)
#define LANDLOCK_ACCESS_FS_READ_DIR (1ULL << 3)
#define LANDLOCK_ACCESS_FS_REMOVE_DIR (1ULL << 4)
#define LANDLOCK_ACCESS_FS_REMOVE_FILE (1ULL << 5)
#define LANDLOCK_ACCESS_FS_MAKE_CHAR (1ULL << 6)
#define LANDLOCK_ACCESS_FS_MAKE_DIR (1ULL << 7)
#define LANDLOCK_ACCESS_FS_MAKE_REG (1ULL << 8)
#define LANDLOCK_ACCESS_FS_MAKE_SOCK (1ULL << 9)
#define LANDLOCK_ACCESS_FS_MAKE_FIFO (1ULL << 10)
#define LANDLOCK_ACCESS_FS_MAKE_BLOCK (1ULL << 11)
#define LANDLOCK_ACCESS_FS_MAKE_SYM (1ULL << 12)
#endif

// Define ABI v2 FS flags
#ifndef LANDLOCK_ACCESS_FS_REFER
#define LANDLOCK_ACCESS_FS_REFER (1ULL << 13)
#endif

// Define ABI v3 FS flags
#ifndef LANDLOCK_ACCESS_FS_TRUNCATE
#define LANDLOCK_ACCESS_FS_TRUNCATE (1ULL << 14)
#endif

// Define ABI v4+ Net flags
#ifndef LANDLOCK_ACCESS_NET_BIND_TCP
#define LANDLOCK_ACCESS_NET_BIND_TCP (1ULL << 0)
#define LANDLOCK_ACCESS_NET_CONNECT_TCP (1ULL << 1)
#endif

// Define ABI v5+ FS flags
#ifndef LANDLOCK_ACCESS_FS_IOCTL_DEV
#define LANDLOCK_ACCESS_FS_IOCTL_DEV (1ULL << 15)
#endif

// Define ABI v6+ Scope flags
#ifndef LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET
#define LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET (1ULL << 0)
#define LANDLOCK_SCOPE_SIGNAL (1ULL << 1)
#endif

struct landlock_ruleset_attr_v7 {
  uint64_t handled_access_fs;
  uint64_t handled_access_net;
  uint64_t handled_scoped;
};

struct landlock_path_beneath_attr {
  uint64_t allowed_access;
  int parent_fd;
};

#ifndef __NR_landlock_create_ruleset
#define __NR_landlock_create_ruleset 444
#endif

#ifndef __NR_landlock_add_rule
#define __NR_landlock_add_rule 445
#endif

#ifndef __NR_landlock_restrict_self
#define __NR_landlock_restrict_self 446
#endif

namespace sandbox2 {

namespace {
using ::sapi::file::JoinPath;
using ::sapi::file_util::fileops::FDCloser;

bool IsDirNode(const MountTree* tree) {
  return tree->has_node() && !tree->node().has_file_node();
}

bool IsWritableNode(const MountTree* tree, const std::string& path) {
  if (!tree->has_node()) {
    return false;
  }
  if (tree->node().has_dir_node()) {
    return tree->node().dir_node().writable();
  }
  if (tree->node().has_file_node()) {
    return tree->node().file_node().writable();
  }
  if (tree->node().has_root_node()) {
    return tree->node().root_node().writable();
  }
  if (tree->node().has_tmpfs_node()) {
    return true;
  }
  SAPI_RAW_LOG(FATAL,
               "Unsupported node type in landlock MountTree traversal: %s",
               path.c_str());
  return false;
}

void AddRulesRecursively(int ruleset_fd, const MountTree* tree,
                         const std::string& path,
                         const std::string& rw_ancestor,
                         const std::string& ro_ancestor,
                         bool allow_write_executable) {
  const bool writable = IsWritableNode(tree, path);
  const bool is_dir = IsDirNode(tree);

  const std::string& farthest_rw_ancestor =
      (tree->has_node() && writable && is_dir && rw_ancestor.empty())
          ? path
          : rw_ancestor;
  const std::string& farthest_ro_ancestor =
      (tree->has_node() && !writable && is_dir && ro_ancestor.empty())
          ? path
          : ro_ancestor;

  for (const auto& entry : tree->entries()) {
    std::string next_path = JoinPath(path, entry.first);
    AddRulesRecursively(ruleset_fd, &entry.second, next_path,
                        farthest_rw_ancestor, farthest_ro_ancestor,
                        allow_write_executable);
  }

  if (!tree->has_node() || tree->node().has_root_node()) {
    return;
  }

  if (!writable && !rw_ancestor.empty()) {
    SAPI_RAW_LOG(
        WARNING,
        "Landlock policy warning: Ancestor path '%s' is writable, "
        "but nested path '%s' is read-only. Landlock 'path beneath' rules "
        "propagate downwards, making '%s' implicitly writable at the LSM "
        "level (though DAC/file permissions still apply).",
        rw_ancestor.c_str(), path.c_str(), path.c_str());
  }
  if (!allow_write_executable && writable && !ro_ancestor.empty()) {
    SAPI_RAW_LOG(
        WARNING,
        "Landlock policy warning: Ancestor path '%s' is read-only (with "
        "execute access), but nested path '%s' is writable. In Landlock v7, "
        "'path beneath' rules propagate downwards without exception, causing "
        "'%s' to implicitly inherit FS_EXECUTE from its ancestor "
        "despite allow_write_executable=false.",
        ro_ancestor.c_str(), path.c_str(), path.c_str());
  }

  FDCloser fd(open(path.c_str(), O_PATH | O_CLOEXEC));
  if (fd.get() < 0 && (errno == ENOENT || errno == EACCES)) {
    SAPI_RAW_LOG(WARNING,
                 "Ignoring non-existing or unreadable path for landlock: %s "
                 "(%s)",
                 path.c_str(), strerror(errno));
    return;
  }
  SAPI_RAW_PCHECK(fd.get() >= 0, "open failed for %s", path.c_str());

  struct landlock_path_beneath_attr path_beneath = {
      .allowed_access = LANDLOCK_ACCESS_FS_READ_FILE,
      .parent_fd = fd.get(),
  };
  if (!writable || allow_write_executable) {
    path_beneath.allowed_access |= LANDLOCK_ACCESS_FS_EXECUTE;
  }
  if (writable) {
    // Include FS_TRUNCATE so writable paths support truncate(), ftruncate(),
    // and open(O_TRUNC)/creat().
    path_beneath.allowed_access |=
        LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_TRUNCATE;
  }
  if (is_dir) {
    path_beneath.allowed_access |= LANDLOCK_ACCESS_FS_READ_DIR;
    if (writable) {
      path_beneath.allowed_access |=
          LANDLOCK_ACCESS_FS_REMOVE_DIR | LANDLOCK_ACCESS_FS_REMOVE_FILE |
          LANDLOCK_ACCESS_FS_MAKE_CHAR | LANDLOCK_ACCESS_FS_MAKE_DIR |
          LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_MAKE_SOCK |
          LANDLOCK_ACCESS_FS_MAKE_FIFO | LANDLOCK_ACCESS_FS_MAKE_BLOCK |
          LANDLOCK_ACCESS_FS_MAKE_SYM | LANDLOCK_ACCESS_FS_REFER;
    }
  }

  SAPI_RAW_PCHECK(
      sandbox2::util::Syscall(
          __NR_landlock_add_rule, ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
          reinterpret_cast<uintptr_t>(&path_beneath), 0) == 0,
      "landlock_add_rule failed for %s", path.c_str());
}
}  // namespace

void EnforceLandlock(const Mounts& mounts) {
  struct landlock_ruleset_attr_v7 ruleset_attr = {
      .handled_access_fs =
          LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_WRITE_FILE |
          LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR |
          LANDLOCK_ACCESS_FS_REMOVE_DIR | LANDLOCK_ACCESS_FS_REMOVE_FILE |
          LANDLOCK_ACCESS_FS_MAKE_CHAR | LANDLOCK_ACCESS_FS_MAKE_DIR |
          LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_MAKE_SOCK |
          LANDLOCK_ACCESS_FS_MAKE_FIFO | LANDLOCK_ACCESS_FS_MAKE_BLOCK |
          LANDLOCK_ACCESS_FS_MAKE_SYM | LANDLOCK_ACCESS_FS_REFER |
          LANDLOCK_ACCESS_FS_TRUNCATE | LANDLOCK_ACCESS_FS_IOCTL_DEV,
      .handled_access_net =
          LANDLOCK_ACCESS_NET_BIND_TCP | LANDLOCK_ACCESS_NET_CONNECT_TCP,
      .handled_scoped =
          LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET | LANDLOCK_SCOPE_SIGNAL,
  };

  FDCloser ruleset_fd(sandbox2::util::Syscall(
      __NR_landlock_create_ruleset, reinterpret_cast<uintptr_t>(&ruleset_attr),
      sizeof(ruleset_attr), 0));
  SAPI_RAW_PCHECK(ruleset_fd.get() >= 0, "landlock_create_ruleset v7 failed");

  auto mount_tree = mounts.GetMountTree();
  AddRulesRecursively(ruleset_fd.get(), &mount_tree, "/", "", "",
                      mounts.GetMountSpecs().allow_write_executable());

  SAPI_RAW_PCHECK(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0,
                  "prctl PR_SET_NO_NEW_PRIVS failed");
  SAPI_RAW_PCHECK(sandbox2::util::Syscall(__NR_landlock_restrict_self,
                                          ruleset_fd.get(), 0) == 0,
                  "landlock_restrict_self failed");
}

bool IsLandlockSupported() {
  // Queries kernel Landlock ABI version at runtime.
  // Returns -1 on failure:
  // - errno == ENOSYS if Landlock is not supported/compiled into the kernel.
  // - errno == EOPNOTSUPP if Landlock is compiled-in but disabled (e.g., via
  //            lsm= boot parameter).
  int abi_version = syscall(__NR_landlock_create_ruleset, nullptr, 0, 1);
  if (abi_version < 7) {
    if (abi_version >= 1) {
      SAPI_RAW_LOG(FATAL, "Landlock ABI v%d detected, but ABI v7 is required.",
                   abi_version);
    }
    return false;
  }
  return true;
}

}  // namespace sandbox2
