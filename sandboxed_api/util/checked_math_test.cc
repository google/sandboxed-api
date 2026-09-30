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

#include "sandboxed_api/util/checked_math.h"

#include <cstddef>
#include <cstdint>
#include <limits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace sapi {
namespace {

using ::testing::Eq;

template <typename T>
class CheckedMathTest : public ::testing::Test {};

using UnsignedTypes =
    ::testing::Types<uint8_t, uint16_t, uint32_t, uint64_t, size_t>;
TYPED_TEST_SUITE(CheckedMathTest, UnsignedTypes);

TYPED_TEST(CheckedMathTest, MultiplyOverflow) {
  using T = TypeParam;
  constexpr T kMax = std::numeric_limits<T>::max();

  T out = 0;
  EXPECT_FALSE(MultiplyOverflow<T>(0, 0, &out));
  EXPECT_THAT(out, Eq(0));

  EXPECT_FALSE(MultiplyOverflow<T>(kMax, 0, &out));
  EXPECT_THAT(out, Eq(0));

  EXPECT_FALSE(MultiplyOverflow<T>(0, kMax, &out));
  EXPECT_THAT(out, Eq(0));

  EXPECT_FALSE(MultiplyOverflow<T>(kMax, 1, &out));
  EXPECT_THAT(out, Eq(kMax));

  EXPECT_FALSE(MultiplyOverflow<T>(6, 7, &out));
  EXPECT_THAT(out, Eq(42));

  EXPECT_TRUE(MultiplyOverflow<T>(kMax, 2, &out));
  EXPECT_TRUE(MultiplyOverflow<T>(kMax, kMax, &out));
  EXPECT_TRUE(MultiplyOverflow<T>((kMax / 8) + 1, 8, &out));
}

TYPED_TEST(CheckedMathTest, MultiplyOverflowFallback) {
  using T = TypeParam;
  constexpr T kMax = std::numeric_limits<T>::max();

  // Evaluate in a constexpr context so signed integer promotion overflow (e.g.
  // for `uint16_t`) is caught as a compile-time error.
  static_assert([] {
    T out = 0;
    return internal::MultiplyOverflowFallback<T>(kMax, kMax, &out);
  }());

  T out = 0;
  EXPECT_FALSE(internal::MultiplyOverflowFallback<T>(0, 0, &out));
  EXPECT_THAT(out, Eq(0));

  EXPECT_FALSE(internal::MultiplyOverflowFallback<T>(kMax, 0, &out));
  EXPECT_THAT(out, Eq(0));

  EXPECT_FALSE(internal::MultiplyOverflowFallback<T>(0, kMax, &out));
  EXPECT_THAT(out, Eq(0));

  EXPECT_FALSE(internal::MultiplyOverflowFallback<T>(kMax, 1, &out));
  EXPECT_THAT(out, Eq(kMax));

  EXPECT_FALSE(internal::MultiplyOverflowFallback<T>(6, 7, &out));
  EXPECT_THAT(out, Eq(42));

  EXPECT_TRUE(internal::MultiplyOverflowFallback<T>(kMax, 2, &out));
  EXPECT_TRUE(internal::MultiplyOverflowFallback<T>(kMax, kMax, &out));
  EXPECT_TRUE(internal::MultiplyOverflowFallback<T>((kMax / 8) + 1, 8, &out));
}

TYPED_TEST(CheckedMathTest, CheckedMultiply) {
  using T = TypeParam;
  constexpr T kMax = std::numeric_limits<T>::max();

  EXPECT_THAT(CheckedMultiply<T>(0, kMax), Eq(0));
  EXPECT_THAT(CheckedMultiply<T>(kMax, 1), Eq(kMax));
  EXPECT_THAT(CheckedMultiply<T>(6, 7), Eq(42));
  EXPECT_DEATH(CheckedMultiply<T>(kMax, 2), "");
}

}  // namespace
}  // namespace sapi
