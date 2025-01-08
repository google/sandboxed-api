// Copyright 2024 Google LLC
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

#ifndef SANDBOXED_API_SANDBOX2_UTIL_DEADLINE_MANAGER_H_
#define SANDBOXED_API_SANDBOX2_UTIL_DEADLINE_MANAGER_H_

#include <cstddef>
#include <memory>

#include "absl/base/no_destructor.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/btree_set.h"
#include "absl/flags/declare.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "sandboxed_api/util/thread.h"

ABSL_DECLARE_FLAG(int, sandbox2_deadline_manager_signal);

namespace sandbox2 {

class DeadlineManager;

// Interface for managing the deadline for a blocking syscall. The syscall
// should be interruptible by a signal. On deadline expiration deadline manager
// repeatedly sends a signal to the thread running potentially running the
// blocking syscall until the provided functor returns. This repetition resolves
// the race between signaling syscall execution and actually invoking the
// blocking syscall.
//
// If the deadline is unlikely to be changed between multiple blocking syscalls,
// it's more efficient to reuse a single registration object.
class DeadlineRegistration {
 public:
  explicit DeadlineRegistration(DeadlineManager& manager);

  DeadlineRegistration(const DeadlineRegistration&) = delete;
  DeadlineRegistration& operator=(const DeadlineRegistration&) = delete;

  DeadlineRegistration(DeadlineRegistration&&) = default;
  DeadlineRegistration& operator=(DeadlineRegistration&&) = default;

  ~DeadlineRegistration();

  // Executes a blocking syscall.
  // The function is executed only if the deadline is not expired already.
  // The syscall will be interrupted after the deadline.
  void ExecuteBlockingSyscall(absl::FunctionRef<void()> blocking_fn);

  // Sets the deadline for the blocking syscall.
  // The deadline is rounded up to the next resolution boundary.
  // Can be called from a different thread concurrently with a running blocking
  // syscall.
  void SetDeadline(absl::Time deadline);

 private:
  friend class DeadlineManager;

  struct Data {
    absl::Mutex mutex;
    // Changed only under both DeadlineManager::queue_mutex_ and Data::mutex.
    absl::Time deadline = absl::InfiniteFuture();
    pid_t ABSL_GUARDED_BY(mutex) tid = -1;
    bool ABSL_GUARDED_BY(mutex) in_blocking_fn = false;
    int ABSL_GUARDED_BY(mutex) notification_attempt = 0;
  };

  DeadlineManager& manager_;
  absl::Time last_deadline_ = absl::InfiniteFuture();
  std::unique_ptr<Data> data_ = std::make_unique<Data>();
};

// Engine for delivering deadline notifications to threads. Runs a separate
// thread which manages all the registered deadlines.
// All deadlines are rounded up to resolution of DeadlineManager (10 ms) for the
// purpose of batching notifications and reducing wakeups of the manager thread.
class DeadlineManager {
 public:
  // Returns the global instance of the deadline manager.
  static DeadlineManager& instance() {
    static absl::NoDestructor<DeadlineManager> manager{
        "deadline_manager-global"};
    return *manager;
  }
  // Creates and starts the manager.
  explicit DeadlineManager(absl::string_view name);

  DeadlineManager(const DeadlineManager&) = delete;
  DeadlineManager& operator=(const DeadlineManager&) = delete;

  DeadlineManager(DeadlineManager&&) = delete;
  DeadlineManager& operator=(DeadlineManager&&) = delete;

  ~DeadlineManager();

  // Adjusts the deadline for the registration.
  // Prefer to use DeadlineRegistration::SetDeadline.
  void AdjustDeadline(DeadlineRegistration& registration, absl::Time deadline);

 private:
  friend class DeadlineRegistration;
  static constexpr absl::Duration kResolution = absl::Milliseconds(10);

  struct ByDeadline {
    bool operator()(const DeadlineRegistration::Data* a,
                    const DeadlineRegistration::Data* b) const {
      return a->deadline < b->deadline || (a->deadline == b->deadline && a < b);
    }
  };

  static void RegisterSignalHandler();
  static void SignalHandler(int signal);
  static void VerifySignalHandler();

  void Register(DeadlineRegistration& registration) {
    absl::MutexLock lock(&registration_mutex_);
    ++registered_deadlines_;
  }
  void Unregister(DeadlineRegistration& registration) {
    {
      absl::MutexLock lock(&queue_mutex_);
      queue_.erase(registration.data_.get());
    }
    absl::MutexLock lock(&registration_mutex_);
    --registered_deadlines_;
  }
  void Run();

  static int signal_nr_;
  sapi::Thread thread_;
  absl::Mutex queue_mutex_;
  bool cancelled_ ABSL_GUARDED_BY(queue_mutex_) = false;
  // We only need an adjustable heap, but there is no widely available
  // implementation ¯\_(ツ)_/¯. Asymptotically it's the same and it should not
  // matter at our scale.
  absl::btree_set<DeadlineRegistration::Data*, ByDeadline> ABSL_GUARDED_BY(
      queue_mutex_) queue_;
  absl::Mutex registration_mutex_;
  size_t registered_deadlines_ ABSL_GUARDED_BY(registration_mutex_) = 0;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_UTIL_DEADLINE_MANAGER_H_
