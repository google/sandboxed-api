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

#include <sched.h>
#include <sys/types.h>

#include <cstdint>
#include <string>

#include "sandboxed_api/sandbox2/forkserver.pb.h"
#include "sandboxed_api/sandbox2/mounts.h"

namespace sandbox2 {

class Namespace final {
 public:
  // Performs the namespace setup (mounts, write the uid_map, etc.).
  static void InitializeNamespaces(uid_t uid, gid_t gid, int32_t clone_flags,
                                   const Mounts& mounts,
                                   const std::string& hostname,
                                   bool avoid_pivot_root,
                                   bool allow_mount_propagation);
  static void InitializeInitialNamespaces(uid_t uid, gid_t gid);

  Namespace(Mounts mounts, std::string hostname, NetNsMode netns_config,
            bool allow_mount_propagation = false);

  NetNsMode netns_config() const { return netns_config_; }

  int32_t clone_flags() const { return clone_flags_; }

  Mounts& mounts() { return mounts_; }
  const Mounts& mounts() const { return mounts_; }

  const std::string& hostname() const { return hostname_; }

  bool allow_mount_propagation() const { return allow_mount_propagation_; }

 private:
  int32_t clone_flags_ = CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWUTS |
                         CLONE_NEWPID | CLONE_NEWIPC | CLONE_NEWNET;
  Mounts mounts_;
  std::string hostname_;
  bool allow_mount_propagation_;
  NetNsMode netns_config_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_NAMESPACE_H_
