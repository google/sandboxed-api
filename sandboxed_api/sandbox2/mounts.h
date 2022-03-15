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

#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/mount_tree.pb.h"

namespace sandbox2 {

namespace internal {

bool IsSameFile(const std::string& path1, const std::string& path2);
bool IsEquivalentNode(const MountTree::Node& n1, const MountTree::Node& n2);
}  // namespace internal

class Mounts {
 public:
  Mounts() {
    MountTree::Node root;
    root.mutable_root_node()->set_writable(false);
    *mount_tree_.mutable_node() = root;
  }

  explicit Mounts(MountTree mount_tree) : mount_tree_(std::move(mount_tree)) {}

  Mounts(const Mounts&) = default;
  Mounts(Mounts&&) = default;
  Mounts& operator=(const Mounts&) = default;
  Mounts& operator=(Mounts&&) = default;

  absl::Status AddFile(absl::string_view path, bool is_ro = true);

  absl::Status AddFileAt(absl::string_view outside, absl::string_view inside,
                         bool is_ro = true);

  absl::Status AddDirectoryAt(absl::string_view outside,
                              absl::string_view inside, bool is_ro = true);

  absl::Status AddMappingsForBinary(const std::string& path,
                                    absl::string_view ld_library_path = {});

  absl::Status AddTmpfs(absl::string_view inside, size_t sz);

  void CreateMounts(const std::string& root_path) const;

  MountTree GetMountTree() const { return mount_tree_; }

  void SetRootWritable() {
    mount_tree_.mutable_node()->mutable_root_node()->set_writable(true);
  }

  bool IsRootReadOnly() const {
    return mount_tree_.has_node() && mount_tree_.node().has_root_node() &&
           !mount_tree_.node().root_node().writable();
  }

  // Lists the outside and inside entries of the input tree in the output
  // parameters, in an ls-like manner. Each entry is traversed in the
  // depth-first order. However, the entries on the same level of hierarchy are
  // traversed in their natural order in the tree. The elements in the output
  // containers match each other pairwise: outside_entries[i] is mounted as
  // inside_entries[i]. The elements of inside_entries are prefixed with either
  // 'R' (read-only) or 'W' (writable).
  void RecursivelyListMounts(std::vector<std::string>* outside_entries,
                             std::vector<std::string>* inside_entries);

  absl::StatusOr<std::string> ResolvePath(absl::string_view path) const;

 private:
  friend class MountTreeTest;

  absl::Status Insert(absl::string_view path, const MountTree::Node& node);

  MountTree mount_tree_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_MOUNTTREE_H_
