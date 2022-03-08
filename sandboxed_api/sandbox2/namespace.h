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

// The sandbox2::Namespace class defines ways of inserting the sandboxed process
// into Linux namespaces.

#ifndef SANDBOXED_API_SANDBOX2_NAMESPACE_H_
#define SANDBOXED_API_SANDBOX2_NAMESPACE_H_

#include <sys/types.h>

#include <cstdint>
#include <memory>
#include <string>

#include "absl/base/macros.h"
#include "sandboxed_api/sandbox2/mounts.h"
#include "sandboxed_api/sandbox2/violation.pb.h"

namespace sandbox2 {

class Namespace final {
 public:
  // Performs the namespace setup (mounts, write the uid_map, etc.).
  static void InitializeNamespaces(uid_t uid, gid_t gid, int32_t clone_flags,
                                   const Mounts& mounts, bool mount_proc,
                                   const std::string& hostname,
                                   bool avoid_pivot_root,
                                   bool allow_mount_propagation);
  static void InitializeInitialNamespaces(uid_t uid, gid_t gid);

  Namespace() = delete;
  Namespace(const Namespace&) = delete;
  Namespace& operator=(const Namespace&) = delete;

  Namespace(bool allow_unrestricted_networking, Mounts mounts,
            std::string hostname, bool allow_mount_propagation);

  void DisableUserNamespace();

  // Returns all needed CLONE_NEW* flags.
  int32_t GetCloneFlags() const;

  // Stores information about this namespace in the protobuf structure.
  void GetNamespaceDescription(NamespaceDescription* pb_description);

  Mounts& mounts() { return mounts_; }
  const Mounts& mounts() const { return mounts_; }

  const std::string& hostname() const { return hostname_; }

  bool allow_mount_propagation() const { return allow_mount_propagation_; }

 private:
  friend class StackTracePeer;

  int32_t clone_flags_;
  Mounts mounts_;
  std::string hostname_;
  bool allow_mount_propagation_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_NAMESPACE_H_
