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

TEST(Test, InPrimStructPointer) {
  PrimitiveStruct s_union_i32 = {
      0, 1, 2, 3, 4.0, 5.0, true, {6}, {7, 8}, ENUM_A, EnumClassType::EC_B};
  EXPECT_EQ(mylib_in_prim_struct_pointer(&s_union_i32), 39.0);

  PrimitiveStruct s_union_f64 = {0,
                                 1,
                                 2,
                                 3,
                                 4.0,
                                 5.0,
                                 false,
                                 {.f64 = 8.5},
                                 {7, 8},
                                 ENUM_B,
                                 EnumClassType::EC_A};
  EXPECT_EQ(mylib_in_prim_struct_pointer(&s_union_f64), 41.5);

  PrimitiveStruct s_neg_inf = {0,
                               1,
                               2,
                               3,
                               -std::numeric_limits<float>::infinity(),
                               5.0,
                               false,
                               {.f64 = 9.0},
                               {7, 8},
                               ENUM_A,
                               EnumClassType::EC_B};
  EXPECT_EQ(mylib_in_prim_struct_pointer(&s_neg_inf),
            -std::numeric_limits<float>::infinity());

  PrimitiveStruct s_nan = {0,
                           1,
                           2,
                           3,
                           4.0,
                           5.0,
                           false,
                           {.f64 = std::numeric_limits<double>::quiet_NaN()},
                           {7, 8},
                           ENUM_A,
                           EnumClassType::EC_B};
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
  PrimitiveStruct s = {0, 1, 2, 3, 4.0, 5.0, true, {6}, {7, 8}, ENUM_A};
  mylib_inout_prim_struct_pointer(&s);

  EXPECT_EQ(s.i8, 0);
  EXPECT_EQ(s.i16, 2);
  EXPECT_EQ(s.i32, 4);
  EXPECT_EQ(s.sz, 6);
  EXPECT_EQ(s.f32, 8.0);
  EXPECT_EQ(s.f64, 10.0);
  EXPECT_EQ(s.u_is_int, true);
  EXPECT_EQ(s.u.i32, 12);
  EXPECT_EQ(s.nested.a, 14);
  EXPECT_EQ(s.nested.b, 16);
  EXPECT_EQ(s.enum_type, ENUM_B);
  EXPECT_EQ(s.enum_class_type, EnumClassType::EC_A);
}

TEST(Test, InPrimStructArray) {
  PrimitiveStruct structs[2] = {
      {0, 1, 2, 3, 4.0, 5.0, true, {6}, {7, 8}, ENUM_A, EnumClassType::EC_B},
      {0,
       2,
       4,
       6,
       8.0,
       10.0,
       false,
       {.f64 = 11.5},
       {14, 16},
       ENUM_B,
       EnumClassType::EC_A}};
  EXPECT_EQ(mylib_in_prim_struct_array(structs, 2), 113.5);
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
  PrimitiveStruct s[2] = {
      {0, 1, 2, 3, 4.0, 5.0, true, {6}, {7, 8}, ENUM_A, EnumClassType::EC_B},
      {0,
       2,
       4,
       6,
       8.0,
       10.0,
       false,
       {.f64 = 11.5},
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
  EXPECT_EQ(s[1].nested.a, 28);
  EXPECT_EQ(s[1].nested.b, 32);
  EXPECT_EQ(s[1].enum_type, ENUM_A);
  EXPECT_EQ(s[1].enum_class_type, EnumClassType::EC_B);
}

}  // namespace
