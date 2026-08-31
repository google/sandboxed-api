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

#include "sandboxed_api/tests/testcases/replaced_library_callbacks.h"

#include <cstdint>
#include <cstring>

#include "gtest/gtest.h"

namespace {

TEST(Test, CallbackWithPrimitives) {
  auto cb = [](int a, int b) -> int { return a + b; };
  EXPECT_EQ(callback_with_primitives(cb, 10, 20), 30);
  EXPECT_EQ(callback_with_primitives(cb, -5, 5), 0);
}

TEST(Test, NullCallbackWithPrimitives) {
  EXPECT_EQ(callback_with_primitives(nullptr, 10, 20), 0);
}

static uint8_t* captured_output = nullptr;

static uint8_t* AllocatingCallback(size_t size, int init_val) {
  captured_output = new uint8_t[size];
  memset(captured_output, init_val, size);
  return captured_output;
}

static void ClearCapturedOutput() { captured_output = nullptr; }

TEST(Test, CallbackReturningBufferNonAlias) {
  uint8_t input[] = {0, 1, 2, 3, 4, 5, 6, 7};

  callback_returning_buffer(input, sizeof(input), AllocatingCallback);
  EXPECT_EQ(memcmp(captured_output, input, sizeof(input)), 0);
  EXPECT_EQ(memcmp(captured_output + sizeof(input), input, sizeof(input)), 0);
  EXPECT_EQ(captured_output[sizeof(input) * 2], 0xCA);
  EXPECT_EQ(captured_output[sizeof(input) * 2 + 1], 0xCA);

  delete[] captured_output;
  ClearCapturedOutput();
}

TEST(Test, CallbackReturningBufferAliasFunctionPointer) {
  uint8_t input[] = {0, 1, 2, 3, 4, 5, 6, 7};

  uint8_t* output =
      ret_alias_func_pointer(input, sizeof(input), AllocatingCallback);

  EXPECT_EQ(output, captured_output);
  EXPECT_EQ(memcmp(output, input, sizeof(input)), 0);
  EXPECT_EQ(memcmp(output + sizeof(input), input, sizeof(input)), 0);
  EXPECT_EQ(output[sizeof(input) * 2], 0xCA);
  EXPECT_EQ(output[sizeof(input) * 2 + 1], 0xCA);

  delete[] output;
  ClearCapturedOutput();
}

TEST(Test, CallbackReturningBufferAliasStdFunction) {
  uint8_t input[] = {0, 1, 2, 3, 4, 5, 6, 7};

  int incr1 = 1;
  int incr2 = 2;
  auto WrapAllocatingCallback = [&](size_t size, int init_val) -> uint8_t* {
    return AllocatingCallback(size, init_val + incr1 + incr2);
  };
  uint8_t* output =
      ret_alias_std_function(input, sizeof(input), WrapAllocatingCallback);
  EXPECT_EQ(output, captured_output);
  EXPECT_EQ(memcmp(output, input, sizeof(input)), 0);
  EXPECT_EQ(memcmp(output + sizeof(input), input, sizeof(input)), 0);
  EXPECT_EQ(output[sizeof(input) * 2], 0xCD);
  EXPECT_EQ(output[sizeof(input) * 2 + 1], 0xCD);

  delete[] output;
  ClearCapturedOutput();
}

TEST(Test, CallbackReturningBufferAliasAnyInvocable) {
  uint8_t input[] = {0, 1, 2, 3, 4, 5, 6, 7};

  int incr1 = 1;
  int incr2 = 1;
  int incr3 = 1;
  int incr4 = 1;
  int incr5 = 1;
  auto WrapAllocatingCallback = [&](size_t size, int init_val) -> uint8_t* {
    return AllocatingCallback(size,
                              init_val + incr1 + incr2 + incr3 + incr4 + incr5);
  };
  uint8_t* output =
      ret_alias_any_invocable(input, sizeof(input), WrapAllocatingCallback);
  EXPECT_EQ(output, captured_output);
  EXPECT_EQ(memcmp(output, input, sizeof(input)), 0);
  EXPECT_EQ(memcmp(output + sizeof(input), input, sizeof(input)), 0);
  EXPECT_EQ(output[sizeof(input) * 2], 0xCF);
  EXPECT_EQ(output[sizeof(input) * 2 + 1], 0xCF);

  delete[] output;
  ClearCapturedOutput();
}

TEST(Test, NullCallbackReturningBuffer) {
  uint8_t input[] = {0, 1, 2, 3, 4, 5, 6, 7};
  // Null callback, but non-null input.
  EXPECT_EQ(ret_alias_func_pointer(input, sizeof(input), nullptr), nullptr);

  // Non-null callback, but null input.
  auto cb = [](size_t size, int init_val) -> uint8_t* {
    if (size > 22) return nullptr;
    uint8_t* output = new uint8_t[size];
    memset(output, init_val, size);
    return output;
  };
  EXPECT_EQ(ret_alias_func_pointer(nullptr, sizeof(input), cb), nullptr);

  // Non-null callback, but the callback returns null.
  uint8_t large_input[] = {0, 1, 2,  3,  4,  5,  6,  7,
                           8, 9, 10, 11, 12, 13, 14, 15};
  EXPECT_EQ(ret_alias_func_pointer(large_input, sizeof(large_input), cb),
            nullptr);
}

int cur_chunk_global = 0;
uint8_t output_buffer_global[12] = {0};

void ClearOutputBufferGlobal() {
  cur_chunk_global = 0;
  memset(output_buffer_global, 0, sizeof(output_buffer_global));
}

uint8_t* NextOutChunkGlobal(size_t chunk_size) {
  uint8_t* chunk = output_buffer_global + (cur_chunk_global * chunk_size);
  ++cur_chunk_global;
  return chunk;
}

TEST(Test, RetAliasCalledMultipleTimes) {
  ClearOutputBufferGlobal();

  size_t chunk_size = 4;
  int num_chunks = sizeof(output_buffer_global) / chunk_size;

  uint8_t* last_chunk = ret_alias_called_multiple_times(
      num_chunks, chunk_size,
      /*return_second_last=*/false, NextOutChunkGlobal);

  EXPECT_EQ(cur_chunk_global, num_chunks);
  EXPECT_EQ(last_chunk, output_buffer_global + (num_chunks - 1) * chunk_size);
  uint8_t expected_output[] = {10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12};
  EXPECT_EQ(memcmp(output_buffer_global, expected_output,
                   sizeof(output_buffer_global)),
            0);

  ClearOutputBufferGlobal();

  uint8_t* second_last_chunk = ret_alias_called_multiple_times(
      num_chunks, chunk_size,
      /*return_second_last=*/true, NextOutChunkGlobal);

  EXPECT_EQ(cur_chunk_global, num_chunks);
  EXPECT_EQ(second_last_chunk,
            output_buffer_global + (num_chunks - 2) * chunk_size);
  EXPECT_EQ(memcmp(output_buffer_global, expected_output,
                   sizeof(output_buffer_global)),
            0);

  ClearOutputBufferGlobal();
}

TEST(Test, MultipleCallbacksOneRetAlias) {
  ClearOutputBufferGlobal();

  size_t chunk_size = 4;
  ASSERT_LT(chunk_size * 2, sizeof(output_buffer_global));
  uint8_t* output = multiple_callbacks_one_ret_alias(
      chunk_size, NextOutChunkGlobal, NextOutChunkGlobal);

  EXPECT_EQ(output, output_buffer_global + chunk_size);
  uint8_t expected_output[] = {0xCA, 0xCA, 0xCA, 0xCA, 0xFE, 0xFE,
                               0xFE, 0xFE, 0,    0,    0,    0};
  EXPECT_EQ(memcmp(output_buffer_global, expected_output,
                   sizeof(output_buffer_global)),
            0);

  ClearOutputBufferGlobal();
}

TEST(Test, WithInputPrimPointer) {
  auto cb = [](const int64_t* input) -> int64_t { return *input * 2; };
  EXPECT_EQ(with_input_prim_pointer(cb, 1LL << 31), 1LL << 33);
}

TEST(Test, WithInputElemSized) {
  auto cb = [](const int* input, size_t in_size) -> int {
    int sum = 0;
    for (size_t i = 0; i < in_size; ++i) {
      sum += input[i];
    }
    return sum;
  };
  EXPECT_EQ(with_input_elem_sized(cb, 5), 10);
}

TEST(Test, WithInputByteSized) {
  auto cb = [](const void* input, size_t num_bytes) -> int {
    int sum = 0;
    const int* int_input = static_cast<const int*>(input);
    size_t num_ints = num_bytes / sizeof(int);
    for (size_t i = 0; i < num_ints; ++i) {
      sum += int_input[i];
    }
    return sum;
  };
  EXPECT_EQ(with_input_byte_sized(cb, 5), 10);
}

TEST(Test, WithInputNullTerm) {
  const char* input = "hello world";
  auto cb = [](const char* input) -> int {
    int sum = 0;
    int i = 0;
    while (input[i] != '\0') {
      sum += input[i];
      ++i;
    }
    return sum;
  };
  EXPECT_EQ(with_input_null_term(cb, input), 1116);
}

TEST(Test, NullWithInputElemSized) {
  EXPECT_EQ(with_input_elem_sized(nullptr, 3), -1);
}

TEST(Test, NullWithInputByteSized) {
  EXPECT_EQ(with_input_byte_sized(nullptr, 3), -1);
}

TEST(Test, NullWithInputNullTerm) {
  EXPECT_EQ(with_input_null_term(nullptr, "test"), -1);
}

}  // namespace
