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

#ifndef SANDBOXED_API_SANDBOX2_TESTCASES_REPLACED_LIBRARY_STRUCT_H_
#define SANDBOXED_API_SANDBOX2_TESTCASES_REPLACED_LIBRARY_STRUCT_H_

#include <cstddef>
#include <cstdint>

enum EnumType {
  ENUM_UNKNOWN,
  ENUM_A,
  ENUM_B,
};

enum class EnumClassType {
  EC_UNKNOWN,
  EC_A,
  EC_B,
};

struct NestedStruct {
  int32_t a;
  int32_t b;
};

struct PrimitiveStruct {
  int8_t i8;
  int16_t i16;
  int32_t i32;
  size_t sz;

  float f32;
  double f64;

  bool u_is_int;
  union {
    int32_t i32;
    double f64;
  } u;

  NestedStruct nested;
  EnumType enum_type;
  EnumClassType enum_class_type;
};

#endif  // SANDBOXED_API_SANDBOX2_TESTCASES_REPLACED_LIBRARY_STRUCT_H_
