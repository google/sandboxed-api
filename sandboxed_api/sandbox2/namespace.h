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
#include "sandboxed_api/sandbox2/setup_latency_breakdown.h"

namespace sandbox2 {

class Namespace final {
 public:
  // Performs the namespace setup (mounts, write the uid_map, etc.).
  static void InitializeNamespaces(uid_t uid, gid_t gid,
                                   const ForkRequest& request,
                                   SetupLatencyBreakdown& latency_breakdown);
  static void InitializeInitialNamespaces(uid_t uid, gid_t gid);
  static void SetupIDMaps(int proc_self_fd, uid_t uid, gid_t gid);

  // Enforces Landlock isolation for the given mounts.
  static void EnforceLandlockIsolation(
      int32_t clone_flags, const Mounts& mounts, uid_t uid, gid_t gid,
      SetupLatencyBreakdown& latency_breakdown);

  static void InitializeSharedPidNamespaces();

  Namespace(Mounts mounts, std::string hostname, NetNsMode netns_config,
            bool use_landlock);

  NetNsMode netns_config() const { return netns_config_; }

  int32_t clone_flags() const { return clone_flags_; }

  Mounts& mounts() { return mounts_; }
  const Mounts& mounts() const { return mounts_; }

  const std::string& hostname() const { return hostname_; }

  bool use_landlock() const { return use_landlock_; }

 private:
  // Unshares a new user namespace and sets up idmaps.
  static void UnshareNestedUserNamespace(int proc_self_fd);

  int32_t clone_flags_ = CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWUTS |
                         CLONE_NEWPID | CLONE_NEWIPC | CLONE_NEWNET;
  Mounts mounts_;
  std::string hostname_;
  NetNsMode netns_config_;
  bool use_landlock_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_NAMESPACE_H_
