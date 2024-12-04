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

#include "sandboxed_api/sandbox2/util/deadline_manager.h"

#include <sys/syscall.h>

#include <ctime>

#include "gtest/gtest.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "sandboxed_api/util/thread.h"

namespace sandbox2 {
namespace {

TEST(DeadlineManagerTest, Basic) {
  DeadlineManager manager("test");
  DeadlineRegistration registration(manager);
  absl::Time start_time = absl::Now();
  struct timespec ts = absl::ToTimespec(absl::Seconds(1));
  registration.SetDeadline(start_time + absl::Milliseconds(100));
  registration.ExecuteBlockingSyscall(
      [&] { ASSERT_EQ(nanosleep(&ts, nullptr), -1); });
  absl::Duration elapsed = absl::Now() - start_time;
  EXPECT_GE(elapsed, absl::Milliseconds(100));
  EXPECT_LE(elapsed, absl::Milliseconds(200));
}

TEST(DeadlineManagerTest, NotifiesUntilFunctionReturns) {
  DeadlineManager manager("test");
  DeadlineRegistration registration(manager);
  absl::Time start_time = absl::Now();
  struct timespec ts = absl::ToTimespec(absl::Seconds(1));
  registration.SetDeadline(start_time + absl::Milliseconds(100));
  registration.ExecuteBlockingSyscall([&] {
    // Double so that it needs to be notified twice.
    ASSERT_EQ(nanosleep(&ts, nullptr), -1);
    ASSERT_EQ(nanosleep(&ts, nullptr), -1);
  });
  absl::Duration elapsed = absl::Now() - start_time;
  EXPECT_GE(elapsed, absl::Milliseconds(100));
  EXPECT_LE(elapsed, absl::Milliseconds(200));
}

TEST(DeadlineManagerTest, DeadlineInThePast) {
  DeadlineManager manager("test");
  DeadlineRegistration registration(manager);
  registration.SetDeadline(absl::InfinitePast());
  registration.ExecuteBlockingSyscall(
      [&] { FAIL() << "Function should not be executed"; });
}

TEST(DeadlineManagerTest, DeadlineSetConcurrently) {
  DeadlineManager manager("test");
  DeadlineRegistration registration(manager);
  absl::Time start_time = absl::Now();
  struct timespec ts = absl::ToTimespec(absl::Seconds(1));
  registration.ExecuteBlockingSyscall([&] {
    sapi::Thread thread([&] {
      absl::SleepFor(absl::Milliseconds(10));
      registration.SetDeadline(start_time + absl::Milliseconds(100));
    });
    ASSERT_EQ(nanosleep(&ts, nullptr), -1);
    thread.Join();
  });
  absl::Duration elapsed = absl::Now() - start_time;
  EXPECT_GE(elapsed, absl::Milliseconds(100));
  EXPECT_LE(elapsed, absl::Milliseconds(200));
}

TEST(DeadlineManagerTest, DeadlineInPastSetConcurrently) {
  DeadlineManager manager("test");
  DeadlineRegistration registration(manager);
  absl::Time start_time = absl::Now();
  struct timespec ts = absl::ToTimespec(absl::Seconds(1));
  registration.ExecuteBlockingSyscall([&] {
    sapi::Thread thread([&] {
      absl::SleepFor(absl::Milliseconds(100));
      registration.SetDeadline(absl::InfinitePast());
    });
    ASSERT_EQ(nanosleep(&ts, nullptr), -1);
    thread.Join();
  });
  absl::Duration elapsed = absl::Now() - start_time;
  EXPECT_GE(elapsed, absl::Milliseconds(100));
  EXPECT_LE(elapsed, absl::Milliseconds(200));
}

TEST(DeadlineManagerTest, DeadlineReset) {
  DeadlineManager manager("test");
  DeadlineRegistration registration(manager);
  absl::Time start_time = absl::Now();
  struct timespec ts = absl::ToTimespec(absl::Milliseconds(200));
  registration.ExecuteBlockingSyscall([&] {
    registration.SetDeadline(absl::InfiniteFuture());
    ASSERT_EQ(nanosleep(&ts, nullptr), 0);
  });
  absl::Duration elapsed = absl::Now() - start_time;
  EXPECT_GE(elapsed, absl::Milliseconds(200));
}

TEST(DeadlineManagerTest, CanBeReusedAfterExpiration) {
  DeadlineManager manager("test");
  DeadlineRegistration registration(manager);
  for (int i = 0; i < 3; ++i) {
    absl::Time start_time = absl::Now();
    struct timespec ts = absl::ToTimespec(absl::Seconds(1));
    registration.SetDeadline(start_time + absl::Milliseconds(100));
    registration.ExecuteBlockingSyscall(
        [&] { ASSERT_EQ(nanosleep(&ts, nullptr), -1); });
    absl::Duration elapsed = absl::Now() - start_time;
    EXPECT_GE(elapsed, absl::Milliseconds(100));
    EXPECT_LE(elapsed, absl::Milliseconds(200));
  }
}

TEST(DeadlineManagerTest, WorksInAThread) {
  DeadlineManager manager("test");
  DeadlineRegistration registration(manager);
  sapi::Thread thread([&] {
    absl::Time start_time = absl::Now();
    struct timespec ts = absl::ToTimespec(absl::Seconds(1));
    registration.SetDeadline(start_time + absl::Milliseconds(100));
    registration.ExecuteBlockingSyscall(
        [&] { ASSERT_EQ(nanosleep(&ts, nullptr), -1); });
    absl::Duration elapsed = absl::Now() - start_time;
    EXPECT_GE(elapsed, absl::Milliseconds(100));
    EXPECT_LE(elapsed, absl::Milliseconds(200));
  });
  thread.Join();
}

}  // namespace
}  // namespace sandbox2
