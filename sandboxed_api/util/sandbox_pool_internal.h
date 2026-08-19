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

#ifndef SANDBOXED_API_UTIL_SANDBOX_POOL_INTERNAL_H_
#define SANDBOXED_API_UTIL_SANDBOX_POOL_INTERNAL_H_

#include <cstddef>
#include <memory>
#include <queue>
#include <string>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace sapi::sandbox_pool_internal {

template <typename SandboxT>
struct PoolEntry {
  std::unique_ptr<SandboxT> sandbox;
  int usage_count = 0;
};

// A thread-safe queue.
template <typename T>
class Queue {
 public:
  Queue() = default;
  virtual ~Queue() = default;

  // Push an item into the queue. Returns false if the queue is stopped.
  bool Push(T value) {
    absl::MutexLock lock(mutex_);
    if (stopped_) {
      return false;
    }

    queue_.push(std::move(value));
    return true;
  }

  // Blocks until an item is ready or Stop() is called.
  // Returns true on success, or false if the queue was stopped.
  bool Pop(T& value) {
    absl::MutexLock lock(mutex_);

    // Wait until the queue is not empty OR the queue is stopped
    mutex_.Await(absl::Condition(this, &Queue::CanPop));

    if (stopped_) {
      return false;
    }

    value = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  // Blocks until an item is ready, the deadline is reached, or Stop() is
  // called. Returns true on success, or false if the queue was stopped or the
  // deadline was reached.
  bool PopWithDeadline(T& value, absl::Time deadline) {
    absl::MutexLock lock(mutex_);

    // Returns true if the condition evaluates to true before the timeout
    // expires, or false if the timeout hits first.
    if (!mutex_.AwaitWithDeadline(absl::Condition(this, &Queue::CanPop),
                                  deadline)) {
      return false;  // Timeout expired
    }

    if (stopped_) {
      return false;  // Queue was stopped
    }

    value = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  // Attempts to pop an item immediately without blocking.
  // Returns true if data was extracted, false if queue was empty or stopped.
  bool TryPop(T& value) {
    absl::MutexLock lock(mutex_);
    if (queue_.empty() || stopped_) {
      return false;
    }

    value = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  // Pops and processes all remaining items in the queue, even if stopped.
  void Drain(absl::AnyInvocable<void(T&&)> fn) {
    absl::MutexLock lock(mutex_);
    while (!queue_.empty()) {
      fn(std::move(queue_.front()));
      queue_.pop();
    }
  }

  // Stops the queue and wakes up all blocked threads
  void Stop() {
    absl::MutexLock lock(mutex_);
    stopped_ = true;
  }

  // Returns the number of items in the queue.
  size_t size() const {
    absl::MutexLock lock(mutex_);
    return queue_.size();
  }

 protected:
  // Condition predicate function for absl::Mutex::Await
  bool CanPop() const { return !queue_.empty() || stopped_; }

  mutable absl::Mutex mutex_;
  std::queue<T> queue_;
  bool stopped_ = false;
};

template <typename T>
struct ExpirableItem {
  T value;
  absl::Time last_used;
};

// A thread-safe queue that stores items with an expiration time.
template <typename T>
class ExpirableQueue : private Queue<ExpirableItem<T>> {
  using Base = Queue<ExpirableItem<T>>;

 public:
  ExpirableQueue() : Base() {}
  virtual ~ExpirableQueue() = default;

  // Push an item into the queue with the current time as the last time used.
  // Returns false if the queue is stopped.
  bool Push(T value) {
    return Base::Push(ExpirableItem<T>{std::move(value), absl::Now()});
  }

  // Pop an item from the queue.
  // Returns true on success, or false if the queue is empty or stopped.
  bool Pop(T& value) {
    ExpirableItem<T> item;
    if (Base::Pop(item)) {
      value = std::move(item.value);
      return true;
    }
    return false;
  }

  // Pop an item from the queue with a deadline.
  // Returns true on success, or false if the queue is empty, stopped, or the
  // deadline is reached.
  bool PopWithDeadline(T& value, absl::Time deadline) {
    ExpirableItem<T> item;
    if (Base::PopWithDeadline(item, std::move(deadline))) {
      value = std::move(item.value);
      return true;
    }
    return false;
  }

  // Attempts to pop an item from the queue without blocking.
  // Returns true on success, or false if the queue is empty or stopped.
  bool TryPop(T& value) {
    ExpirableItem<T> item;
    if (Base::TryPop(item)) {
      value = std::move(item.value);
      return true;
    }
    return false;
  }

  // Pop an item from the queue if it is expired or the queue size is greater
  // than the minimum queue size.
  // Returns true on success, or false if the queue is empty or stopped.
  bool PopWhenExpired(T& value, const absl::Duration& max_age,
                      size_t min_queue_size) {
    ExpirableItem<T> item;
    absl::MutexLock lock(Base::mutex_);
    auto predicate = [&]() {
      return Base::stopped_ ||
             (Base::queue_.size() > min_queue_size &&
              absl::Now() - Base::queue_.front().last_used >= max_age);
    };
    while (!Base::mutex_.AwaitWithDeadline(
        absl::Condition(&predicate),
        Base::queue_.size() <= min_queue_size
            ? absl::Now() + max_age
            : Base::queue_.front().last_used + max_age)) {
      // Spin until the condition is met or the deadline is reached.
    }
    if (Base::stopped_) {
      return false;
    }
    value = std::move(Base::queue_.front().value);
    Base::queue_.pop();
    return true;
  }

  void Drain(absl::AnyInvocable<void(T&&)> fn) {
    Base::Drain([&fn](ExpirableItem<T>&& item) { fn(std::move(item.value)); });
  }

  using Base::size;
  using Base::Stop;
};

}  // namespace sapi::sandbox_pool_internal

#endif  // SANDBOXED_API_UTIL_SANDBOX_POOL_INTERNAL_H_
