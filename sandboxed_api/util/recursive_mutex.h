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

#ifndef SANDBOXED_API_UTIL_RECURSIVE_MUTEX_H_
#define SANDBOXED_API_UTIL_RECURSIVE_MUTEX_H_

#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>

#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

namespace sapi {

// A mutex that the thread holding it can lock again. RPC channels use it so
// that a host callback can call back into the sandboxee on the thread that is
// serving the callback.
class ABSL_LOCKABLE RecursiveMutex {
 public:
  RecursiveMutex() = default;
  ~RecursiveMutex() = default;

  RecursiveMutex(const RecursiveMutex&) = delete;
  RecursiveMutex& operator=(const RecursiveMutex&) = delete;

  void Lock() ABSL_EXCLUSIVE_LOCK_FUNCTION() {
    pid_t tid = GetCachedTID();
    if (owner_.load(std::memory_order_acquire) != tid) {
      mutex_.Lock();
      owner_.store(tid, std::memory_order_release);
    }
    ++recursion_depth_;
  }

  void Unlock() ABSL_UNLOCK_FUNCTION() {
    if (--recursion_depth_ > 0) {
      return;
    }
    owner_.store(0, std::memory_order_release);
    mutex_.Unlock();
  }

 private:
  static pid_t GetCachedTID() {
    thread_local pid_t tid = 0;
    if (ABSL_PREDICT_FALSE(tid == 0)) {
      tid = syscall(__NR_gettid);
    }
    return tid;
  }

  absl::Mutex mutex_;
  std::atomic<pid_t> owner_{0};
  int recursion_depth_ = 0;
};

class ABSL_SCOPED_LOCKABLE RecursiveMutexLock {
 public:
  explicit RecursiveMutexLock(RecursiveMutex& mutex)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mutex)
      : recursive_mutex_(mutex) {
    recursive_mutex_.Lock();
  }
  ~RecursiveMutexLock() ABSL_UNLOCK_FUNCTION() { recursive_mutex_.Unlock(); }

  RecursiveMutexLock(const RecursiveMutexLock&) = delete;
  RecursiveMutexLock& operator=(const RecursiveMutexLock&) = delete;

 private:
  RecursiveMutex& recursive_mutex_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_UTIL_RECURSIVE_MUTEX_H_
