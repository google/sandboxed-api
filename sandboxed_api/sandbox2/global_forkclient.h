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

// StartGlobalForkServer() is called early in a process using a constructor, so
// it can fork() safely (in a single-threaded context)

#ifndef SANDBOXED_API_SANDBOX2_GLOBAL_FORKCLIENT_H_
#define SANDBOXED_API_SANDBOX2_GLOBAL_FORKCLIENT_H_

#include <sys/types.h>

#include <bitset>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/flags/declare.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/fork_client.h"
#include "sandboxed_api/sandbox2/forkserver.pb.h"

namespace sandbox2 {

enum class GlobalForkserverStartMode {
  kOnDemand,
  // MUST be the last element
  kNumGlobalForkserverStartModes,
};

class GlobalForkClient {
 public:
  GlobalForkClient(int fd, pid_t pid)
      : comms_(fd), fork_client_(pid, &comms_) {}

  static pid_t SendRequest(const ForkRequest& request, int exec_fd,
                           int comms_fd, int user_ns_fd = -1,
                           pid_t* init_pid = nullptr)
      ABSL_LOCKS_EXCLUDED(instance_mutex_);
  static pid_t GetPid() ABSL_LOCKS_EXCLUDED(instance_mutex_);

  static void EnsureStarted() ABSL_LOCKS_EXCLUDED(instance_mutex_) {
    EnsureStarted(GlobalForkserverStartMode::kOnDemand);
  }
  static void Shutdown() ABSL_LOCKS_EXCLUDED(instance_mutex_);
  static bool IsStarted() ABSL_LOCKS_EXCLUDED(instance_mutex_);

 private:
  friend void StartGlobalForkserverFromLibCtor();

  static void ForceStart() ABSL_LOCKS_EXCLUDED(instance_mutex_);
  static void EnsureStarted(GlobalForkserverStartMode mode)
      ABSL_LOCKS_EXCLUDED(instance_mutex_);
  static void EnsureStartedLocked(GlobalForkserverStartMode mode)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(instance_mutex_);

  static absl::Mutex instance_mutex_;
  static GlobalForkClient* instance_ ABSL_GUARDED_BY(instance_mutex_);

  Comms comms_;
  ForkClient fork_client_;
};

class GlobalForkserverStartModeSet {
 public:
  static constexpr size_t kSize = static_cast<size_t>(
      GlobalForkserverStartMode::kNumGlobalForkserverStartModes);

  GlobalForkserverStartModeSet() {}
  explicit GlobalForkserverStartModeSet(GlobalForkserverStartMode value) {
    value_[static_cast<size_t>(value)] = true;
  }
  GlobalForkserverStartModeSet& operator|=(GlobalForkserverStartMode value) {
    value_[static_cast<size_t>(value)] = true;
    return *this;
  }
  GlobalForkserverStartModeSet operator|(
      GlobalForkserverStartMode value) const {
    GlobalForkserverStartModeSet rv(*this);
    rv |= value;
    return rv;
  }
  bool contains(GlobalForkserverStartMode value) const {
    return value_[static_cast<size_t>(value)];
  }
  bool empty() { return value_.none(); }

 private:
  std::bitset<kSize> value_;
};

bool AbslParseFlag(absl::string_view text, GlobalForkserverStartModeSet* out,
                   std::string* error);
std::string AbslUnparseFlag(GlobalForkserverStartModeSet in);

}  // namespace sandbox2

ABSL_DECLARE_FLAG(sandbox2::GlobalForkserverStartModeSet,
                  sandbox2_forkserver_start_mode);
ABSL_DECLARE_FLAG(std::string, sandbox2_forkserver_binary_path);

#endif  // SANDBOXED_API_SANDBOX2_GLOBAL_FORKCLIENT_H_
