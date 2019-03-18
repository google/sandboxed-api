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

#ifndef SANDBOXED_API_SANDBOX2_MOUNTTREE_H_
#define SANDBOXED_API_SANDBOX2_MOUNTTREE_H_

#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/mounttree.pb.h"
#include "sandboxed_api/util/status.h"

namespace sandbox2 {

class Mounts {
 public:
  Mounts() = default;
  explicit Mounts(MountTree mount_tree) : mount_tree_(std::move(mount_tree)) {}

  ::sapi::Status AddFile(absl::string_view path, bool is_ro = true);

  ::sapi::Status AddFileAt(absl::string_view outside, absl::string_view inside,
                           bool is_ro = true);

  ::sapi::Status AddDirectoryAt(absl::string_view outside,
                                absl::string_view inside, bool is_ro = true);

  ::sapi::Status AddMappingsForBinary(const std::string& path,
                                      absl::string_view ld_library_path = {});

  ::sapi::Status AddTmpfs(absl::string_view inside, size_t sz);

  void CreateMounts(const std::string& root_path) const;

  MountTree GetMountTree() const { return mount_tree_; }

 private:
  friend class MountTreeTest;
  ::sapi::Status Insert(absl::string_view path, const MountTree::Node& node);
  MountTree mount_tree_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_MOUNTTREE_H_
