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

#include <cstddef>
#include <string>

#include "gtest/gtest.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/tests/testcases/replaced_library.h"
#include "sandboxed_api/tests/testcases/replaced_library_callbacks.h"
#include "sandboxed_api/tests/testcases/replaced_library_complex_size.h"
#include "sandboxed_api/tests/testcases/replaced_library_reverse_callbacks.h"
#include "sandboxed_api/tests/testcases/replaced_library_struct_sync.h"
#include "sandboxed_api/tests/testcases/replaced_library_thunk.h"

namespace {

TEST(MergedSandboxTest, MultipleSandboxedLibrariesShareSingleSandbox) {
  if constexpr (sapi::sanitizers::IsTSan()) {
    EXPECT_FALSE(mylib_is_sandboxed());
  } else {
    EXPECT_TRUE(mylib_is_sandboxed());
  }
  EXPECT_EQ(mylib_add(20, 22), 42);

  char buf[4] = {};
  mylib_fill_bytes(buf, 'A', sizeof(buf));
  EXPECT_EQ(std::string(buf, sizeof(buf)), "AAAA");

  MixedStruct mixed = {};
  InitMixedStruct(&mixed);
  EXPECT_NE(mixed.sandbox_opaque_data, nullptr);
  EXPECT_EQ(MungeMixedStruct(&mixed), 123 + 0x12345678);

  int values[3] = {10, 20, 30};
  OneOrTwoBuffs buffs = {
      .kind = kOne,
      .one_buff = {.buff = values, .size = 3},
  };
  EXPECT_EQ(sum_buffs(&buffs), 60);

  EXPECT_EQ(
      callback_with_primitives([](int a, int b) { return a + b; }, 10, 20), 30);
  EXPECT_EQ(with_input_elem_sized(
                [](const int* input, size_t in_size) {
                  int sum = 0;
                  for (size_t i = 0; i < in_size; ++i) {
                    sum += input[i];
                  }
                  return sum;
                },
                5),
            10);

  auto adder = get_adder_fn(0);
  ASSERT_NE(adder, nullptr);
  EXPECT_EQ(adder(15, 27), 42);
  EXPECT_EQ(apply_twice(adder, 10, 5), 20);
}

}  // namespace
