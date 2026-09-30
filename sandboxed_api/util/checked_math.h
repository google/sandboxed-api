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

#ifndef SANDBOXED_API_UTIL_CHECKED_MATH_H_
#define SANDBOXED_API_UTIL_CHECKED_MATH_H_

#include <concepts>

#include "absl/log/check.h"

namespace sapi {
namespace internal {

// Portable fallback for `MultiplyOverflow`, split into a separate function so
// it can be unit-tested even when `__builtin_mul_overflow` is available.
template <std::unsigned_integral T>
  requires(!std::same_as<T, bool>)
constexpr bool MultiplyOverflowFallback(T a, T b, T* out) {
  // Multiply by `1u` so narrow unsigned types (e.g. `uint16_t`) promote to
  // `unsigned int` instead of signed `int`, avoiding signed overflow UB.
  *out = static_cast<T>(1u * a * b);
  return b != 0 && *out / b != a;
}

}  // namespace internal

// Multiplies `a` and `b` into `*out`, returning true on overflow.
template <std::unsigned_integral T>
  requires(!std::same_as<T, bool>)
constexpr bool MultiplyOverflow(T a, T b, T* out) {
#if __has_builtin(__builtin_mul_overflow)
  return __builtin_mul_overflow(a, b, out);
#else
  return internal::MultiplyOverflowFallback(a, b, out);
#endif
}

// Multiplies `a` and `b`, terminating the process on overflow.
template <std::unsigned_integral T>
  requires(!std::same_as<T, bool>)
T CheckedMultiply(T a, T b) {
  T prod = 0;
  CHECK(!MultiplyOverflow(a, b, &prod));
  return prod;
}

}  // namespace sapi

#endif  // SANDBOXED_API_UTIL_CHECKED_MATH_H_
