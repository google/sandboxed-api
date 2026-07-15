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

#ifndef SANDBOXED_API_SANDBOX2_MOUNTTREE_H_
#define SANDBOXED_API_SANDBOX2_MOUNTTREE_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/mount_tree.pb.h"

namespace sandbox2 {

namespace internal {

bool IsSameFile(const std::string& path1, const std::string& path2);
bool IsWritable(const MountTree::Node& node);
bool HasSameTarget(const MountTree::Node& n1, const MountTree::Node& n2);
bool IsEquivalentNode(const MountTree::Node& n1, const MountTree::Node& n2);

class HashableMountSpecs {
 public:
  explicit HashableMountSpecs(const MountSpecs& mount_specs)
      : mount_specs_(mount_specs) {}
  HashableMountSpecs(const HashableMountSpecs&) = default;
  HashableMountSpecs(HashableMountSpecs&&) = default;
  HashableMountSpecs& operator=(const HashableMountSpecs&) = default;
  HashableMountSpecs& operator=(HashableMountSpecs&&) = default;

  bool operator==(const HashableMountSpecs& other) const {
    return MountSpecsEquals(mount_specs_, other.mount_specs_);
  }

  template <typename H>
  friend H AbslHashValue(H h, const HashableMountSpecs& self) {
    h = H::combine(std::move(h), self.mount_specs_.allow_mount_propagation(),
                   self.mount_specs_.allow_write_executable());
    return MountTreeHash(std::move(h), self.mount_specs_.mount_tree());
  }

  const MountSpecs& mount_specs() const { return mount_specs_; }

 private:
  static bool MountNodeEquals(const MountTree::Node& n1,
                              const MountTree::Node& n2);
  static bool MountTreeEquals(const MountTree& tree1, const MountTree& tree2);
  static bool MountSpecsEquals(const MountSpecs& specs1,
                               const MountSpecs& specs2);

  template <typename H>
  static H MountNodeHash(H h, const MountTree::Node& node) {
    h = H::combine(std::move(h), node.node_case());
    switch (node.node_case()) {
      case MountTree::Node::kFileNode:
        h = H::combine(std::move(h), node.file_node().outside(),
                       node.file_node().writable());
        break;
      case MountTree::Node::kDirNode:
        h = H::combine(std::move(h), node.dir_node().outside(),
                       node.dir_node().writable(),
                       node.dir_node().allow_mount_propagation());
        break;
      case MountTree::Node::kRootNode:
        h = H::combine(std::move(h), node.root_node().writable());
        break;
      case MountTree::Node::kTmpfsNode:
        h = H::combine(std::move(h), node.tmpfs_node().tmpfs_options());
        break;
      case MountTree::Node::NODE_NOT_SET:
        break;
    }
    return h;
  }

  template <typename H>
  static H MountTreeHash(H h, const MountTree& tree) {
    h = H::combine(std::move(h), tree.index(), tree.ignore_non_existing());
    h = MountNodeHash(std::move(h), tree.node());
    std::vector<std::string> keys;
    keys.reserve(tree.entries().size());
    for (const auto& [key, _] : tree.entries()) {
      keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());
    for (const std::string& key : keys) {
      h = H::combine(std::move(h), key);
      h = MountTreeHash(std::move(h), tree.entries().at(key));
    }
    return h;
  }

  MountSpecs mount_specs_;
};

}  // namespace internal

class Mounts {
 public:
  Mounts() {
    MountTree::Node root;
    root.mutable_root_node()->set_writable(false);
    *mount_specs_.mutable_mount_tree()->mutable_node() = root;
  }

  explicit Mounts(MountSpecs mount_specs)
      : mount_specs_(std::move(mount_specs)) {}

  Mounts(const Mounts&) = default;
  Mounts(Mounts&&) = default;
  Mounts& operator=(const Mounts&) = default;
  Mounts& operator=(Mounts&&) = default;

  absl::Status AddFile(absl::string_view path, bool is_ro = true) {
    return AddFileAt(path, path, is_ro);
  }

  absl::Status AddFileAt(absl::string_view outside, absl::string_view inside,
                         bool is_ro = true);

  absl::Status AddDirectory(absl::string_view path, bool is_ro = true) {
    return AddDirectoryAt(path, path, is_ro);
  }

  absl::Status AddDirectoryAt(absl::string_view outside,
                              absl::string_view inside, bool is_ro = true);

  absl::Status AddMappingsForBinary(const std::string& path,
                                    absl::string_view ld_library_path = {});

  absl::Status AddTmpfs(absl::string_view inside, size_t sz);

  absl::Status AllowMountPropagation(absl::string_view inside);

  absl::Status Remove(absl::string_view path);

  void CreateMounts(const std::string& root_path) const;

  const MountTree& GetMountTree() const { return mount_specs_.mount_tree(); }
  const MountSpecs& GetMountSpecs() const { return mount_specs_; }

  void SetRootWritable() {
    mount_specs_.mutable_mount_tree()
        ->mutable_node()
        ->mutable_root_node()
        ->set_writable(true);
  }

  bool IsRootReadOnly() const {
    const MountTree& mount_tree = mount_specs_.mount_tree();
    return mount_tree.has_node() && mount_tree.node().has_root_node() &&
           !mount_tree.node().root_node().writable();
  }

  // Lists the outside and inside entries of the input tree in the output
  // parameters, in an ls-like manner. Each entry is traversed in the
  // depth-first order. However, the entries on the same level of hierarchy are
  // traversed in their natural order in the tree. The elements in the output
  // containers match each other pairwise: outside_entries[i] is mounted as
  // inside_entries[i]. The elements of inside_entries are prefixed with either
  // 'R' (read-only) or 'W' (writable).
  void RecursivelyListMounts(std::vector<std::string>* outside_entries,
                             std::vector<std::string>* inside_entries) const;

  void AllowWriteExecutable() { mount_specs_.set_allow_write_executable(true); }
  void AllowMountPropagation() {
    mount_specs_.set_allow_mount_propagation(true);
  }

  absl::StatusOr<std::string> ResolvePath(absl::string_view path) const;

 private:
  friend class MountTreeTest;

  absl::StatusOr<MountTree::Node> GetNode(absl::string_view path);
  absl::Status Insert(absl::string_view path, const MountTree::Node& node);

  MountSpecs mount_specs_;
  int64_t mount_index_ = 0;  // Used to keep track of the mount insertion order
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_MOUNTTREE_H_
