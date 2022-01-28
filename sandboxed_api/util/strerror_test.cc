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

#include "sandboxed_api/util/strerror.h"

#include <atomic>
#include <cerrno>
#include <string>
#include <thread>  // NOLINT(build/c++11)
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/match.h"

namespace sapi {
namespace {

using ::testing::Eq;
using ::testing::StrEq;

TEST(StrErrorTest, ValidErrorCode) {
  errno = EAGAIN;
  EXPECT_THAT(StrError(EINTR), StrEq(strerror(EINTR)));
  EXPECT_THAT(errno, Eq(EAGAIN));
}

TEST(StrErrorTest, InvalidErrorCode) {
  errno = EBUSY;
  EXPECT_THAT(StrError(-1), StrEq("Unknown error -1"));
  EXPECT_THAT(errno, Eq(EBUSY));
}

TEST(StrErrorTest, MultipleThreads) {
  // In this test, we will start up 2 threads and have each one call StrError
  // 1000 times, each time with a different errnum. We expect that
  // StrError(errnum) will return a string equal to the one returned by
  // strerror(errnum), if the code is known. Since strerror is known to be
  // thread-hostile, collect all the expected strings up front.
  constexpr int kNumCodes = 1000;
  std::vector<std::string> expected_strings(kNumCodes);
  for (int i = 0; i < kNumCodes; ++i) {
    expected_strings[i] = strerror(i);
  }

  std::atomic<int> counter{0};
  auto thread_fun = [&counter, &expected_strings]() {
    for (int i = 0; i < kNumCodes; ++i) {
      ++counter;
      std::string value = StrError(i);
      if (!absl::StartsWith(value, "Unknown error ")) {
        EXPECT_THAT(StrError(i), StrEq(expected_strings[i]));
      }
    }
  };

  constexpr int kNumThreads = 100;
  std::vector<std::thread> threads;
  for (int i = 0; i < kNumThreads; ++i) {
    threads.push_back(std::thread(thread_fun));
  }
  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_THAT(counter, Eq(kNumThreads * kNumCodes));
}

}  // namespace
}  // namespace sapi
