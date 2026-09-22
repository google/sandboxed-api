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

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
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

    queue_.push_back(std::move(value));
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

    PopLocked(value, End::kFront);
    return true;
  }

  // Blocks until an item is ready, the deadline is reached, or Stop() is
  // called. Returns true on success, or false if the queue was stopped or the
  // deadline was reached.
  bool PopWithDeadline(T& value, absl::Time deadline) {
    return PopWithDeadline(value, deadline, End::kFront);
  }

  // Same as PopWithDeadline(), but pops the most recently pushed item.
  bool PopNewestWithDeadline(T& value, absl::Time deadline) {
    return PopWithDeadline(value, deadline, End::kBack);
  }

  // Attempts to pop an item immediately without blocking.
  // Returns true if data was extracted, false if queue was empty or stopped.
  bool TryPop(T& value) { return TryPop(value, End::kFront); }

  // Same as TryPop(), but pops the most recently pushed item. Reusing the
  // hottest item keeps caches warm and, since items are pushed in chronological
  // order, it also leaves the least recently used items at the front where
  // PopIf() can find them.
  bool TryPopNewest(T& value) { return TryPop(value, End::kBack); }

  // Attempts to pop the front (least recently pushed) item, but only if `pred`
  // accepts it. Never blocks. `pred` is called as `pred(front, size)` with the
  // queue lock held, and so must not call back into the queue.
  // Returns true if an item was extracted, or false if the queue was empty or
  // stopped, or if `pred` rejected the front item.
  template <typename Pred>
  bool PopIf(T& value, Pred pred) {
    absl::MutexLock lock(mutex_);
    if (queue_.empty() || stopped_ || !pred(queue_.front(), queue_.size())) {
      return false;
    }

    PopLocked(value, End::kFront);
    return true;
  }

  // Blocks until the queue is stopped or `deadline` is reached.
  // Returns true if the queue was stopped, false if the deadline was reached.
  bool AwaitStopWithDeadline(absl::Time deadline) {
    absl::MutexLock lock(mutex_);
    return mutex_.AwaitWithDeadline(absl::Condition(&stopped_), deadline);
  }

  // Blocks until the queue holds more than `min_size` items, or is stopped.
  // Returns true if the queue was stopped.
  bool AwaitStopOrGrowth(size_t min_size) {
    absl::MutexLock lock(mutex_);
    auto grown = [this, min_size] {
      return stopped_ || queue_.size() > min_size;
    };
    mutex_.Await(absl::Condition(&grown));
    return stopped_;
  }

  // Pops and processes all remaining items in the queue, even if stopped.
  void Drain(absl::AnyInvocable<void(T&&)> fn) {
    absl::MutexLock lock(mutex_);
    while (!queue_.empty()) {
      fn(std::move(queue_.front()));
      queue_.pop_front();
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

 private:
  // Which end of the queue an item is taken from.
  enum class End { kFront, kBack };

  // Condition predicate function for absl::Mutex::Await
  bool CanPop() const { return !queue_.empty() || stopped_; }

  void PopLocked(T& value, End end) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    if (end == End::kFront) {
      value = std::move(queue_.front());
      queue_.pop_front();
    } else {
      value = std::move(queue_.back());
      queue_.pop_back();
    }
  }

  bool TryPop(T& value, End end) {
    absl::MutexLock lock(mutex_);
    if (queue_.empty() || stopped_) {
      return false;
    }

    PopLocked(value, end);
    return true;
  }

  bool PopWithDeadline(T& value, absl::Time deadline, End end) {
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

    PopLocked(value, end);
    return true;
  }

  mutable absl::Mutex mutex_;
  std::deque<T> queue_ ABSL_GUARDED_BY(mutex_);
  bool stopped_ ABSL_GUARDED_BY(mutex_) = false;
};

template <typename T>
struct ExpirableItem {
  T value;
  absl::Time last_used;
};

// A thread-safe queue that stores items with an expiration time.
template <typename T>
class ExpirableQueue {
 public:
  ExpirableQueue() = default;

  // Push an item into the queue with the current time as the last time used.
  // Returns false if the queue is stopped.
  bool Push(T value) {
    return queue_.Push(ExpirableItem<T>{std::move(value), absl::Now()});
  }

  // Both pops take the most recently pushed item. Reusing the warmest sandbox
  // keeps the live working set small instead of rotating through every sandbox
  // in the pool: on a latency-sensitive workload that reuses its sandboxes,
  // this is worth ~5% of wall time and ~14 points of cycles/op, for the same
  // instruction count.
  // It also leaves the oldest items at the front, where `PopWhenExpired` looks.

  // Pop the most recently pushed item from the queue with a deadline.
  // Returns true on success, or false if the queue is empty, stopped, or the
  // deadline is reached.
  bool PopWithDeadline(T& value, absl::Time deadline) {
    ExpirableItem<T> item;
    if (queue_.PopNewestWithDeadline(item, deadline)) {
      value = std::move(item.value);
      return true;
    }
    return false;
  }

  // Attempts to pop the most recently pushed item without blocking.
  // Returns true on success, or false if the queue is empty or stopped.
  bool TryPop(T& value) {
    ExpirableItem<T> item;
    if (queue_.TryPopNewest(item)) {
      value = std::move(item.value);
      return true;
    }
    return false;
  }

  // Blocks until the oldest item has been idle for at least `max_age` while
  // the queue holds more than `min_queue_size` items, then pops it.
  // `min_queue_size` is a floor: items are never popped if doing so would
  // shrink the queue to `min_queue_size` or below, no matter how old they are.
  // An empty queue is not a terminal condition; the call keeps waiting.
  // Returns true once an item has been popped, or false if the queue was
  // stopped while waiting.
  bool PopWhenExpired(T& value, absl::Duration max_age, size_t min_queue_size) {
    while (true) {
      // When the queue is empty, PopIf does not invoke the predicate; default
      // to waiting for `max_age` since no item pushed later can expire sooner.
      absl::Time deadline = absl::Now() + max_age;
      ExpirableItem<T> item;
      bool popped =
          queue_.PopIf(item, [&](const ExpirableItem<T>& front, size_t size) {
            if (size <= min_queue_size) {
              deadline = absl::InfiniteFuture();
              return false;
            }
            deadline = front.last_used + max_age;
            return absl::Now() >= deadline;
          });
      if (popped) {
        value = std::move(item.value);
        return true;
      }
      bool stopped = deadline == absl::InfiniteFuture()
                         ? queue_.AwaitStopOrGrowth(min_queue_size)
                         : queue_.AwaitStopWithDeadline(deadline);
      if (stopped) {
        return false;
      }
    }
  }

  void Drain(absl::AnyInvocable<void(T&&)> fn) {
    queue_.Drain([&fn](ExpirableItem<T>&& item) { fn(std::move(item.value)); });
  }

  size_t size() const { return queue_.size(); }
  void Stop() { queue_.Stop(); }

 private:
  Queue<ExpirableItem<T>> queue_;
};

}  // namespace sapi::sandbox_pool_internal

#endif  // SANDBOXED_API_UTIL_SANDBOX_POOL_INTERNAL_H_
