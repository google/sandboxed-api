// Copyright 2025 Google LLC
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

#include "sandboxed_api/tests/testcases/replaced_library.h"

#include <cstring>
#include <limits>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/tests/testcases/replaced_library_enum.h"
#include "sandboxed_api/tests/testcases/replaced_library_struct.h"

namespace {

TEST(Test, Add) {
  EXPECT_EQ(mylib_add(1, 2), 3);
  EXPECT_EQ(mylib_add(-1, -2), -3);
  EXPECT_EQ(mylib_add(std::numeric_limits<int>::min() + 1, -1),
            std::numeric_limits<int>::min());
}

TEST(Test, Enum) {
  EXPECT_EQ(mylib_take_enum(MYLIB_ENUM_VALUE_1), MYLIB_ENUM_VALUE_1);
}

TEST(Test, HostOpaquePtr) {
  static int global_int;
  int x;
  mylib_take_host_opaque_ptr(&x);
  mylib_take_host_opaque_ptr(&global_int);
}

TEST(Test, Copy) {
  EXPECT_EQ(mylib_copy(""), "");
  EXPECT_EQ(mylib_copy("hello"), "hello");
  std::string src = "hello";
  std::string dst = "there";
  mylib_copy({src.data(), src.size() - 1}, dst);
  EXPECT_EQ(dst, "hell");

  char src_buf[5] = {'h', 'e', 'l', 'l', 'o'};
  char dst_buf[5] = {'t', 'h', 'e', 'r', 'e'};
  mylib_copy_raw(src_buf + 1, dst_buf + 1, 3);
  EXPECT_EQ(std::string(dst_buf, sizeof(dst_buf)), "telle");
}

TEST(Test, NullTerminatedStrlen) {
  EXPECT_EQ(mylib_strlen(""), 0);
  EXPECT_EQ(mylib_strlen("hello"), 5);

  char buf[12] = {'h', 'e', 'l', 'l', 'o', '\n', 't', 'h', 'e', 'r', 'e', '\0'};
  EXPECT_EQ(mylib_strlen(buf), 11);
}

TEST(Test, ReturnGlobalConstCStr) {
  const char* cstr_zero_a = mylib_get_const_c_str(0);
  EXPECT_EQ(std::strcmp(cstr_zero_a, "zero"), 0);
  EXPECT_EQ(std::strcmp(mylib_get_const_c_str(1), "one"), 0);
  EXPECT_EQ(std::strcmp(mylib_get_const_c_str(2), "two"), 0);
  EXPECT_EQ(std::strcmp(mylib_get_const_c_str(3), "other"), 0);
  EXPECT_EQ(std::strcmp(mylib_get_const_c_str(4), "other"), 0);

  // Also a call with the same argument as an earlier call.
  const char* cstr_zero_b = mylib_get_const_c_str(0);
  EXPECT_EQ(std::strcmp(cstr_zero_b, "zero"), 0);
  // May not be required to have the same address, but could be something
  // libraries end up relying on.
  EXPECT_EQ(cstr_zero_a, cstr_zero_b);

  // Try a different function that also returns a const char*.
  EXPECT_EQ(std::strcmp(mylib_get_other_c_str(0), "zero"), 0);
  EXPECT_EQ(std::strcmp(mylib_get_other_c_str(1), "nonzero"), 0);
}

TEST(Test, InOutparamGlobalConstCStr) {
  mylib_get_inoutparam_c_str(nullptr);

  const char* in_out = nullptr;
  mylib_get_inoutparam_c_str(&in_out);
  EXPECT_EQ(in_out, nullptr);

  in_out = "odd";
  mylib_get_inoutparam_c_str(&in_out);
  EXPECT_EQ(std::strcmp(in_out, "even"), 0);
  mylib_get_inoutparam_c_str(&in_out);
  EXPECT_EQ(std::strcmp(in_out, "odd"), 0);

  mylib_get_outparam_c_str(0, nullptr);

  const char* out_0a = nullptr;
  mylib_get_outparam_c_str(0, &out_0a);
  EXPECT_EQ(std::strcmp(out_0a, "zero"), 0);
  const char* out_1 = nullptr;
  mylib_get_outparam_c_str(1, &out_1);
  EXPECT_EQ(std::strcmp(out_1, "nonzero"), 0);

  const char* out_0b = nullptr;
  mylib_get_outparam_c_str(0, &out_0b);
  EXPECT_EQ(std::strcmp(out_0b, "zero"), 0);
  EXPECT_EQ(out_0a, out_0b);
}

TEST(Test, MultipleInOutparamGlobalConstCStr) {
  const char* out = nullptr;
  const char* out2 = nullptr;
  mylib_get_in_outparam_c_str("odd", &out, &out2);
  EXPECT_EQ(std::strcmp(out, "even"), 0);
  EXPECT_EQ(std::strcmp(out2, "flipped_to_even"), 0);
  mylib_get_in_outparam_c_str("even", &out, &out2);
  EXPECT_EQ(std::strcmp(out, "odd"), 0);
  EXPECT_EQ(std::strcmp(out2, "flipped_to_odd"), 0);
}

TEST(Test, AliasOutparamCharBuffer) {
  char buf[10] = {0};
  char* ret = mylib_fill_outbuffer_returning_alias(buf, 'a', sizeof(buf));
  EXPECT_EQ(ret, buf);
  EXPECT_EQ(std::memcmp(ret, "aaaaaaaaaa", 10), 0);

  char* ret2 =
      mylib_fill_outbuffer_returning_alias(buf + 2, 'b', sizeof(buf) - 2);
  EXPECT_EQ(ret2, ret + 2);
  EXPECT_EQ(std::memcmp(ret, "aabbbbbbbb", 10), 0);
}

TEST(Test, AliasOutparamStruct) {
  PrimitiveStruct s;
  PrimitiveStruct* ret = mylib_struct_returning_alias(&s, 1);
  EXPECT_EQ(ret, &s);
  EXPECT_EQ(ret->i8, 1);
  EXPECT_EQ(ret->i16, (1) + (1 << 8));
  EXPECT_EQ(ret->i32, (1) + (1 << 8) + (1 << 16) + (1 << 24));

  PrimitiveStruct* ret2 = mylib_struct_returning_alias(&s, -1);
  EXPECT_EQ(ret2, nullptr);
}

TEST(Test, InPrimStructPointer) {
  PrimitiveStruct s_union_i32 = {0,      1,      2,      3,
                                 4.0,    5.0,    true,   {6},
                                 {1, 2}, {7, 8}, ENUM_A, EnumClassType::EC_B};
  EXPECT_EQ(mylib_in_prim_struct_pointer(&s_union_i32), 42.0);

  PrimitiveStruct s_union_f64 = {0,      1,      2,      3,
                                 4.0,    5.0,    false,  {.f64 = 8.5},
                                 {1, 2}, {7, 8}, ENUM_B, EnumClassType::EC_A};
  EXPECT_EQ(mylib_in_prim_struct_pointer(&s_union_f64), 44.5);

  PrimitiveStruct s_neg_inf = {0,
                               1,
                               2,
                               3,
                               -std::numeric_limits<float>::infinity(),
                               5.0,
                               false,
                               {.f64 = 9.0},
                               {7, 8},
                               {1, 2},
                               ENUM_A,
                               EnumClassType::EC_B};
  EXPECT_EQ(mylib_in_prim_struct_pointer(&s_neg_inf),
            -std::numeric_limits<float>::infinity());

  PrimitiveStruct s_nan = {
      0,      1,      2,      3,
      4.0,    5.0,    false,  {.f64 = std::numeric_limits<double>::quiet_NaN()},
      {1, 2}, {7, 8}, ENUM_A, EnumClassType::EC_B};
  EXPECT_THAT(mylib_in_prim_struct_pointer(&s_nan), testing::IsNan());
}

TEST(Test, OutPrimStructPointer) {
  PrimitiveStruct s;
  mylib_out_prim_struct_pointer(&s);

  EXPECT_EQ(s.i8, 1);
  EXPECT_EQ(s.i16, 2);
  EXPECT_EQ(s.i32, 3);
  EXPECT_EQ(s.sz, 4);
  EXPECT_EQ(s.f32, 5.0);
  EXPECT_EQ(s.f64, 6.0);
  EXPECT_EQ(s.u_is_int, false);
  EXPECT_EQ(s.u.f64, 7.0);
  EXPECT_EQ(s.nested.a, 8);
  EXPECT_EQ(s.nested.b, 9);
  EXPECT_EQ(s.enum_type, ENUM_A);
  EXPECT_EQ(s.enum_class_type, EnumClassType::EC_B);
}

TEST(Test, InOutPrimStructPointer) {
  PrimitiveStruct s = {0, 1, 2, 3, 4.0, 5.0, true, {6}, {2, 1}, {7, 8}, ENUM_A};
  mylib_inout_prim_struct_pointer(&s);

  EXPECT_EQ(s.i8, 0);
  EXPECT_EQ(s.i16, 2);
  EXPECT_EQ(s.i32, 4);
  EXPECT_EQ(s.sz, 6);
  EXPECT_EQ(s.f32, 8.0);
  EXPECT_EQ(s.f64, 10.0);
  EXPECT_EQ(s.u_is_int, true);
  EXPECT_EQ(s.u.i32, 12);
  EXPECT_EQ(s.non_trailing_array[0], 4);
  EXPECT_EQ(s.non_trailing_array[1], 2);
  EXPECT_EQ(s.nested.a, 14);
  EXPECT_EQ(s.nested.b, 16);
  EXPECT_EQ(s.enum_type, ENUM_B);
  EXPECT_EQ(s.enum_class_type, EnumClassType::EC_A);
}

TEST(Test, InPrimStructArray) {
  PrimitiveStruct structs[2] = {{0,
                                 1,
                                 2,
                                 3,
                                 4.0,
                                 5.0,
                                 true,
                                 {6},
                                 {1, 2},
                                 {7, 8},
                                 ENUM_A,
                                 EnumClassType::EC_B},
                                {0,
                                 2,
                                 4,
                                 6,
                                 8.0,
                                 10.0,
                                 false,
                                 {.f64 = 11.5},
                                 {3, 4},
                                 {14, 16},
                                 ENUM_B,
                                 EnumClassType::EC_A}};
  EXPECT_EQ(mylib_in_prim_struct_array(structs, 2), 123.5);
}

TEST(Test, OutPrimStructArray) {
  PrimitiveStruct s[2] = {{}, {}};
  mylib_out_prim_struct_array(s, 2);

  for (int i = 0; i < 2; ++i) {
    EXPECT_EQ(s[i].i8, 1);
    EXPECT_EQ(s[i].i16, 2);
    EXPECT_EQ(s[i].i32, 3);
    EXPECT_EQ(s[i].sz, 4);
    EXPECT_EQ(s[i].f32, 5.0);
    EXPECT_EQ(s[i].f64, 6.0);
    EXPECT_EQ(s[i].u_is_int, false);
    EXPECT_EQ(s[i].u.f64, 7.0);
    EXPECT_EQ(s[i].nested.a, 8);
    EXPECT_EQ(s[i].nested.b, 9);
    EXPECT_EQ(s[i].enum_type, ENUM_A);
    EXPECT_EQ(s[i].enum_class_type, EnumClassType::EC_B);
  }
}

TEST(Test, InOutPrimStructArray) {
  PrimitiveStruct s[2] = {{0,
                           1,
                           2,
                           3,
                           4.0,
                           5.0,
                           true,
                           {6},
                           {1, 2},
                           {7, 8},
                           ENUM_A,
                           EnumClassType::EC_B},
                          {0,
                           2,
                           4,
                           6,
                           8.0,
                           10.0,
                           false,
                           {.f64 = 11.5},
                           {3, 4},
                           {14, 16},
                           ENUM_B,
                           EnumClassType::EC_A}};
  mylib_inout_prim_struct_array(s, 2);

  EXPECT_EQ(s[0].i8, 0);
  EXPECT_EQ(s[0].i16, 2);
  EXPECT_EQ(s[0].i32, 4);
  EXPECT_EQ(s[0].sz, 6);
  EXPECT_EQ(s[0].f32, 8.0);
  EXPECT_EQ(s[0].f64, 10.0);
  EXPECT_EQ(s[0].u_is_int, true);
  EXPECT_EQ(s[0].u.i32, 12);
  EXPECT_EQ(s[0].non_trailing_array[0], 2);
  EXPECT_EQ(s[0].non_trailing_array[1], 4);
  EXPECT_EQ(s[0].nested.a, 14);
  EXPECT_EQ(s[0].nested.b, 16);
  EXPECT_EQ(s[0].enum_type, ENUM_B);
  EXPECT_EQ(s[0].enum_class_type, EnumClassType::EC_A);

  EXPECT_EQ(s[1].i8, 0);
  EXPECT_EQ(s[1].i16, 4);
  EXPECT_EQ(s[1].i32, 8);
  EXPECT_EQ(s[1].sz, 12);
  EXPECT_EQ(s[1].f32, 16.0);
  EXPECT_EQ(s[1].f64, 20.0);
  EXPECT_EQ(s[1].u_is_int, false);
  EXPECT_EQ(s[1].u.f64, 23.0);
  EXPECT_EQ(s[1].non_trailing_array[0], 6);
  EXPECT_EQ(s[1].non_trailing_array[1], 8);
  EXPECT_EQ(s[1].nested.a, 28);
  EXPECT_EQ(s[1].nested.b, 32);
  EXPECT_EQ(s[1].enum_type, ENUM_A);
  EXPECT_EQ(s[1].enum_class_type, EnumClassType::EC_B);
}

}  // namespace
