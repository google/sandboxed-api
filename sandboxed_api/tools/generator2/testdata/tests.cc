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

// Test file trying to cover as much of DWARF entry combinations as possible.
// Base for testing.
// As we are tracking types related to function calls, types of interest should
// be passed as arguments, returned by function or be part of structure
// dependency chain

#include "sandboxed_api/tools/generator2/testdata/tests.h"

namespace a {
namespace b {

class ExampleClass {
 private:
  int a_;
  int b_;

 public:
  int GetSum() const { return a_ + b_; }
};

}  // namespace b
}  // namespace a

extern "C" {

// Simple types
bool function_using_simple_types(unsigned char a1,       // NOLINT
                                 signed char a2,         // NOLINT
                                 unsigned short a3,      // NOLINT
                                 signed short a4,        // NOLINT
                                 unsigned int a5,        // NOLINT
                                 signed int a6,          // NOLINT
                                 unsigned long a7,       // NOLINT
                                 signed long a8,         // NOLINT
                                 unsigned long long a9,  // NOLINT
                                 signed long long a10    // NOLINT
) {
  return a1 ? true : false;
}

bool function_using_simple_types_continued(float a1, double a2,
                                           long double a3) {
  return a1 ? true : false;
}

// Class usage
int function_using_class(const a::b::ExampleClass* ptr_to_class) {
  return ptr_to_class->GetSum();
}

// Typedef usage
typedef unsigned int uint;
typedef uint* uint_p;
typedef uint_p* uint_pp;
typedef char** char_pp;
typedef long int long_arr[8];  // NOLINT
typedef void (*function_p)(uint, uint_p, uint_pp);
typedef void (*function_p2)(void (*)(int, char), void*);
typedef int function_3(int a, int b);

typedef union {
  int a;
  char b;
} union_1;

typedef struct {
  function_p a;
  function_p2 b;
  void (*c)(int, long);  // NOLINT
  uint d;
  uint_pp e;
  struct struct_2* f;
} struct_t;

// Using defined types so these end up in debug symbols
uint function_using_typedefs(uint_p a1, uint_pp a2, function_p a3,
                             function_p2* a4, struct_t* a5, char_pp a6,
                             long_arr* a7, function_3* a8) {
  return 1337 + a5->d + a8(1, 3);
}

int function_using_union(union_1* a1) { return a1->a; }

// Pointer usage
unsigned char* function_using_pointers(int* a1, unsigned char* a2,
                                       unsigned char a3, const char* a4) {
  return a2;
}

uint* function_returning_pointer() { return reinterpret_cast<uint*>(0x1337); }

void function_returning_void(int* a) { *a = 1337; }

// Structures
struct __attribute__((__packed__)) struct_1 {
  uint a;
  char b;
  uint c;
  char d;
};

struct struct_2 {
  uint a;
  char b;
  uint c;
  char d;
};

struct struct_3 {
  uint partially_defined_struct_so_field_is_invisible;
};

#define COEF_BITS_SIZE 16
struct struct_4 {
  char a[4];
  int b;
  union {
    uint a;
    char* b;
  } c;
  struct {
    uint a;
    char* b;
  } d;
  function_p func_1;
  // tests for const + ptr issues
  const char* const* const_1;
  const char** const_2;
  char* const* const_3;
  int (*coef_bits)[COEF_BITS_SIZE];
};

int function_using_structures(struct struct_1* a1, struct struct_2* a2,
                              struct struct_3* a3, struct struct_4* a4) {
  return a1->a + a2->a + a4->b;
}

// Tests type loop case typedef -> struct -> fn_ptr -> typedef
struct struct_6_def;
typedef struct struct_6_def struct_6;
typedef struct_6* struct_6p;
typedef void (*function_p3)(struct_6p);
struct struct_6_def {
  function_p3 fn;
};

void function_using_type_loop(struct_6p a1) { a1->fn(a1); }

// Tests struct-in-struct case that fails if we generate forward declarations
// for every structure
struct struct_7_part_def {
  int x;
  int y;
  void (*fn)(void);
};
typedef struct struct_7_part_def s7part;

struct struct_7_def {
  s7part part;
  int x;
};

typedef struct struct_7_def* s7p;

void function_using_incomplete(s7p a1) { a1->part.fn(); }

// Tests for enums
enum Enumeration { ONE, TWO, THREE };
typedef enum Numbers { UNKNOWN, FIVE = 5, SE7EN = 7 } Nums;
typedef enum { SIX = 6, TEN = 10 } SixOrTen;
enum class Color : long long { RED, GREEN = 20, BLUE };  // NOLINT
enum struct Direction { LEFT = 'l', RIGHT = 'r' };

int function_using_enums(Enumeration a1, SixOrTen a2, Color a3, Direction a4,
                         Nums a5) {
  switch (a1) {
    case Enumeration::ONE:
      return Numbers::SE7EN;
    case Enumeration::TWO:
      return a2;
    default:
      return FIVE;
  }
}

}  // extern "C"
