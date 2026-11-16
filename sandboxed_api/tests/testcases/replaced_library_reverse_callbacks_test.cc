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

#include "sandboxed_api/tests/testcases/replaced_library_reverse_callbacks.h"

#include <stdint.h>

#include "gtest/gtest.h"

// End-to-end tests for callbacks in both directions against
// `replaced_library_reverse_callbacks`.

namespace {

TEST(ForwardCallback, HostFunctionIsCalledByTheLibrary) {
  // `apply_twice` calls `cb(cb(a, b), b)`, so a callback that is genuinely
  // reentered is the baseline the reverse direction has to match.
  auto host_add = [](int a, int b) { return a + b; };
  EXPECT_EQ(apply_twice(host_add, 2, 3), 8);

  auto host_multiply = [](int a, int b) { return a * b; };
  EXPECT_EQ(apply_twice(host_multiply, 2, 3), 18);
}

TEST(SandboxeeFunctionAddress, IsStableAndDistinct) {
  // `get_adder` is a test probe, not a shape any library should have: it
  // hands back a raw address so that the pool tests can drive the binding
  // entry point directly. A registry keyed on that address is only sound if
  // the address is the same on a later call, so that is worth pinning here
  // rather than discovering later.
  uintptr_t add_first = get_adder(0);
  uintptr_t multiply = get_adder(1);
  uintptr_t add_again = get_adder(0);

  ASSERT_NE(add_first, 0u);
  ASSERT_NE(multiply, 0u);
  EXPECT_EQ(add_first, add_again);
  EXPECT_NE(add_first, multiply);

  // An index nobody implements stays null rather than becoming some non-null
  // address.
  EXPECT_EQ(get_adder(99), 0u);
}

// A callback handed back out of the sandbox. Unlike everything above, these
// go through the library's own signature, so they read the same and mean the
// same whether or not there is a sandbox underneath.

TEST(ReturnedCallback, CanBeCalled) {
  int (*add)(int, int) = get_adder_fn(0);
  ASSERT_NE(add, nullptr);
  EXPECT_EQ(add(2, 3), 5);

  int (*multiply)(int, int) = get_adder_fn(1);
  ASSERT_NE(multiply, nullptr);
  EXPECT_EQ(multiply(2, 3), 6);
}

TEST(ReturnedCallback, NullStaysNull) {
  // A library's `if (cb)` has to keep working across the boundary.
  EXPECT_EQ(get_adder_fn(99), nullptr);
}

TEST(ReturnedCallback, CanBePassedBackIn) {
  // A callback the sandbox implements, handed out and then handed straight back
  // in as an argument. In real code this is the pattern where a library exposes
  // default callbacks (such as a default allocator or comparator) that the
  // caller can pass back into the library's own entry points. Sandboxed, that
  // is a host thunk going back down and being called from inside the very
  // library it came from.
  EXPECT_EQ(apply_twice(get_adder_fn(0), 2, 3), 8);
  EXPECT_EQ(apply_twice(get_adder_fn(1), 2, 3), 18);
}

TEST(ReturnedCallback, IsInterchangeableWithAHostFunction) {
  // Same call, same callback typedef, once with each kind of callback.
  auto host_add = [](int a, int b) { return a + b; };
  EXPECT_EQ(apply_twice(host_add, 2, 3), 8);
  EXPECT_EQ(apply_twice(get_adder_fn(0), 2, 3), 8);
}

TEST(ReturnedCallback, AskingTwiceGivesTheSameCallback) {
  // Equality of function pointers is what a library's "is this still my
  // hook?" check rests on, so the substitution has to preserve it.
  EXPECT_EQ(get_adder_fn(0), get_adder_fn(0));
  EXPECT_NE(get_adder_fn(0), get_adder_fn(1));
}

}  // namespace
