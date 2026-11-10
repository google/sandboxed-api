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

#include <stdint.h>

#include "gtest/gtest.h"
#include "sandboxed_api/lwbox/runtime/lwbox_runtime.h"
#include "sandboxed_api/tests/testcases/replaced_library_reverse_callbacks.h"

// The questions the hand-written spike answers, asked again of the machinery
// the generator emits for the `adder` callback typedef.
//
// Unlike the spike, this test only exists in the sandboxed configuration. The
// pool and its bind entry point are internals of the generated host shim, and
// the unsandboxed build has no shim: there, a sandboxee function address is
// already a host-callable function pointer and there is nothing to bind. So
// this is a plain cc_test against the sandboxed library rather than half of a
// cc_sandboxed_library_test pair.

namespace {

extern "C" const sapi::lwbox::ThunkPool* sapi_thunk_pool_adder();
extern "C" uintptr_t sapi_bind_thunk_adder(uintptr_t sandboxee_fn);

using AdderFn = int (*)(int, int);

AdderFn BindAdder(uintptr_t sandboxee_fn) {
  return reinterpret_cast<AdderFn>(sapi_bind_thunk_adder(sandboxee_fn));
}

TEST(GeneratedThunkPool, PoolDescribesItsCallbackTypedef) {
  const sapi::lwbox::ThunkPool* pool = sapi_thunk_pool_adder();
  ASSERT_NE(pool, nullptr);
  EXPECT_EQ(pool->typedef_name, "adder");
  EXPECT_FALSE(pool->thunks.empty());
  for (uintptr_t thunk : pool->thunks) {
    EXPECT_NE(thunk, 0u);
  }
}

TEST(GeneratedThunkPool, BoundThunkCallsTheSandboxeeFunction) {
  // The reverse crossing proper: host code calls what looks like an ordinary
  // function pointer, and a function inside the sandbox runs.
  AdderFn add = BindAdder(get_adder(0));
  ASSERT_NE(add, nullptr);
  EXPECT_EQ(add(2, 3), 5);

  AdderFn multiply = BindAdder(get_adder(1));
  ASSERT_NE(multiply, nullptr);
  EXPECT_EQ(multiply(2, 3), 6);
}

TEST(GeneratedThunkPool, NullIsNotACallback) {
  // A library's `if (cb)` has to keep working, so nothing is bound and no slot
  // is spent.
  EXPECT_EQ(sapi_bind_thunk_adder(0), 0u);
}

TEST(GeneratedThunkPool, BindingTheSameFunctionTwiceReusesTheSlot) {
  // A fixed-size pool does not go far if every binding takes a new slot.
  uintptr_t first = sapi_bind_thunk_adder(get_adder(0));
  uintptr_t again = sapi_bind_thunk_adder(get_adder(0));
  uintptr_t other = sapi_bind_thunk_adder(get_adder(1));

  EXPECT_EQ(first, again);
  EXPECT_NE(first, other);
}

TEST(GeneratedThunkPool, ThunkIsInterchangeableWithAHostFunction) {
  // The same library call, once with a host function and once with a thunk
  // standing in for a sandboxee one. Inside `apply_twice` the sandboxee calls
  // back up into the thunk, which calls back down into the sandbox: the
  // nesting the design needs, now through generated code.
  auto host_add = [](int a, int b) { return a + b; };
  EXPECT_EQ(apply_twice(host_add, 2, 3), 8);
  EXPECT_EQ(apply_twice(BindAdder(get_adder(0)), 2, 3), 8);
}

}  // namespace
