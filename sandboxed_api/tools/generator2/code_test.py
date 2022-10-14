# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Tests for code."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from absl.testing import absltest
from absl.testing import parameterized
from clang import cindex
import code
import code_test_util

CODE = """
typedef int(fun*)(int,int);
extern "C" int function_a(int x, int y) { return x + y; }
extern "C" int function_b(int a, int b) { return a + b; }

struct a {
  void (*fun_ptr)(char, long);
}
"""


def analyze_string(content, path='tmp.cc', limit_scan_depth=False):
  """Returns Analysis object for in memory content."""
  return analyze_strings(path, [(path, content)], limit_scan_depth)


def analyze_strings(path, unsaved_files, limit_scan_depth=False):
  """Returns Analysis object for in memory content."""
  return code.Analyzer._analyze_file_for_tu(path, None, False, unsaved_files,
                                            limit_scan_depth)


class CodeAnalysisTest(parameterized.TestCase):

  def testInMemoryFile(self):
    translation_unit = analyze_string(CODE)
    self.assertIsNotNone(translation_unit._tu.cursor)

  def testSimpleASTTraversal(self):
    translation_unit = analyze_string(CODE)

    structs = 0
    functions = 0
    params = 0
    typedefs = 0

    for cursor in translation_unit._walk_preorder():
      if cursor.kind == cindex.CursorKind.FUNCTION_DECL:
        functions += 1
      elif cursor.kind == cindex.CursorKind.STRUCT_DECL:
        structs += 1
      elif cursor.kind == cindex.CursorKind.PARM_DECL:
        params += 1
      elif cursor.kind == cindex.CursorKind.TYPEDEF_DECL:
        typedefs += 1

    self.assertEqual(functions, 2)
    self.assertEqual(structs, 1)
    self.assertEqual(params, 8)
    self.assertEqual(typedefs, 1)

  def testParseSkipFunctionBodies(self):
    function_body = 'extern "C"  int function(bool a1) { return a1 ? 1 : 2; }'
    translation_unit = analyze_string(function_body)
    for cursor in translation_unit._walk_preorder():
      if cursor.kind == cindex.CursorKind.FUNCTION_DECL:
        # cursor.get_definition() is None when we skip parsing function bodies
        self.assertIsNone(cursor.get_definition())

  def testExternC(self):
    translation_unit = analyze_string('extern "C" int function(char* a);')
    cursor_kinds = [
        x.kind
        for x in translation_unit._walk_preorder()
        if x.kind != cindex.CursorKind.MACRO_DEFINITION
    ]
    self.assertListEqual(cursor_kinds, [
        cindex.CursorKind.TRANSLATION_UNIT, cindex.CursorKind.UNEXPOSED_DECL,
        cindex.CursorKind.FUNCTION_DECL, cindex.CursorKind.PARM_DECL
    ])

  @parameterized.named_parameters(
      ('1:', '/tmp/test.h', 'tmp', 'tmp/test.h'),
      ('2:', '/a/b/c/d/tmp/test.h', 'c/d', 'c/d/tmp/test.h'),
      ('3:', '/tmp/test.h', None, '/tmp/test.h'),
      ('4:', '/tmp/test.h', '', '/tmp/test.h'),
      ('5:', '/tmp/test.h', 'xxx', 'xxx/test.h'),
  )
  def testGetIncludes(self, path, prefix, expected):
    function_body = 'extern "C" int function(bool a1) { return a1 ? 1 : 2; }'
    translation_unit = analyze_string(function_body)
    for cursor in translation_unit._walk_preorder():
      if cursor.kind == cindex.CursorKind.FUNCTION_DECL:
        fn = code.Function(translation_unit, cursor)
        fn.get_absolute_path = lambda: path
        self.assertEqual(fn.get_include_path(prefix), expected)

  def testCodeGeneratorOutput(self):
    body = """
      extern "C" {
        int function_a(int x, int y) { return x + y; }

        int types_1(bool a0, unsigned char a1, char a2, unsigned short a3, short a4);
        int types_2(int a0, unsigned int a1, long a2, unsigned long a3);
        int types_3(long long a0, unsigned long long a1, float a2, double a3);
        int types_4(signed char a0, signed short a1, signed int a2, signed long a3);
        int types_5(signed long long a0, long double a1);
        void types_6(char* a0);
      }
    """
    functions = [
        'function_a', 'types_1', 'types_2', 'types_3', 'types_4', 'types_5',
        'types_6'
    ]
    generator = code.Generator([analyze_string(body)])
    result = generator.generate('Test', functions, 'sapi::Tests', None, None)
    self.assertMultiLineEqual(code_test_util.CODE_GOLD, result)

  def testElaboratedArgument(self):
    body = """
      struct x { int a; };
      extern "C" int function(struct x a) { return a.a; }
    """
    generator = code.Generator([analyze_string(body)])
    with self.assertRaisesRegex(ValueError, r'Elaborate.*mapped.*'):
      generator.generate('Test', ['function'], 'sapi::Tests', None, None)

  def testElaboratedArgument2(self):
    body = """
      typedef struct { int a; char b; } x;
      extern "C" int function(x a) { return a.a; }
    """
    generator = code.Generator([analyze_string(body)])
    with self.assertRaisesRegex(ValueError, r'Elaborate.*mapped.*'):
      generator.generate('Test', ['function'], 'sapi::Tests', None, None)

  def testGetMappedType(self):
    body = """
      typedef unsigned int uint;
      typedef uint* uintp;
      extern "C" uint function(uintp a) { return *a; }
    """
    generator = code.Generator([analyze_string(body)])
    result = generator.generate('Test', [], 'sapi::Tests', None, None)
    self.assertMultiLineEqual(code_test_util.CODE_GOLD_MAPPED, result)

  @parameterized.named_parameters(
      ('1:', '/tmp/test.h', '_TMP_TEST_H_'),
      ('2:', 'tmp/te-st.h', 'TMP_TE_ST_H_'),
      ('3:', 'tmp/te-st.h.gen', 'TMP_TE_ST_H_'),
      ('4:', 'xx/genfiles/tmp/te-st.h', 'TMP_TE_ST_H_'),
      ('5:', 'xx/genfiles/tmp/te-st.h.gen', 'TMP_TE_ST_H_'),
      ('6:', 'xx/genfiles/.gen/tmp/te-st.h', '_GEN_TMP_TE_ST_H_'),
  )
  def testGetHeaderGuard(self, path, expected):
    self.assertEqual(code.get_header_guard(path), expected)

  @parameterized.named_parameters(
      ('function with return value and arguments',
       'extern "C" int function(bool arg_bool, char* arg_ptr);',
       ['arg_bool', 'arg_ptr']),
      ('function without return value and no arguments',
       'extern "C" void function();', []),
  )
  def testArgumentNames(self, body, names):
    generator = code.Generator([analyze_string(body)])
    functions = generator._get_functions()
    self.assertLen(functions, 1)
    self.assertLen(functions[0].argument_types, len(names))
    # Extra check for generation, in case rendering throws error for this test.
    generator.generate('Test', [], 'sapi::Tests', None, None)
    for t in functions[0].argument_types:
      self.assertIn(t.name, names)

  def testStaticFunctions(self):
    body = 'static int function() { return 7; };'
    generator = code.Generator([analyze_string(body)])
    self.assertEmpty(generator._get_functions())

  def testEnumGeneration(self):
    body = """
      enum ProcessStatus {
        OK = 0,
        ERROR = 1,
      };

      extern "C" ProcessStatus ProcessDatapoint(ProcessStatus status) {
        return status;
      }
    """
    generator = code.Generator([analyze_string(body)])
    result = generator.generate('Test', [], 'sapi::Tests', None, None)
    self.assertMultiLineEqual(code_test_util.CODE_ENUM_GOLD, result)

  def testTypeEq(self):
    body = """
    typedef unsigned int uint;
    extern "C" void function(uint a1, uint a2, char a3);
    """
    generator = code.Generator([analyze_string(body)])
    functions = generator._get_functions()
    self.assertLen(functions, 1)

    args = functions[0].arguments()
    self.assertLen(args, 3)
    self.assertEqual(args[0], args[1])
    self.assertNotEqual(args[0], args[2])
    self.assertNotEqual(args[1], args[2])

    self.assertLen(set(args), 2)
    # Extra check for generation, in case rendering throws error for this test.
    generator.generate('Test', [], 'sapi::Tests', None, None)

  def testTypedefRelatedTypes(self):
    body = """
      typedef unsigned int uint;
      typedef uint* uint_p;
      typedef uint_p* uint_pp;

      typedef struct data {
        int a;
        int b;
      } data_s;
      typedef data_s* data_p;

      extern "C" uint function_using_typedefs(uint_p a1, uint_pp a2, data_p a3);
    """
    generator = code.Generator([analyze_string(body)])
    functions = generator._get_functions()
    self.assertLen(functions, 1)

    args = functions[0].arguments()
    self.assertLen(args, 3)

    types = args[0].get_related_types()
    names = [t._clang_type.spelling for t in types]
    self.assertLen(types, 2)
    self.assertSameElements(names, ['uint_p', 'uint'])

    types = args[1].get_related_types()
    names = [t._clang_type.spelling for t in types]
    self.assertLen(types, 3)
    self.assertSameElements(names, ['uint_pp', 'uint_p', 'uint'])

    types = args[2].get_related_types()
    names = [t._clang_type.spelling for t in types]
    self.assertLen(types, 2)
    self.assertSameElements(names, ['data_s', 'data_p'])

    # Extra check for generation, in case rendering throws error for this test.
    generator.generate('Test', [], 'sapi::Tests', None, None)

  def testTypedefDuplicateType(self):
    body = """
      typedef struct data {
        int a;
        int b;
      } data_s;

      struct s {
        struct data* f1;
      };

      extern "C" uint function_using_typedefs(struct s* a1, data_s* a2);
    """
    generator = code.Generator([analyze_string(body)])
    functions = generator._get_functions()
    self.assertLen(functions, 1)

    args = functions[0].arguments()
    self.assertLen(args, 2)

    types = generator._get_related_types()
    self.assertLen(generator.translation_units[0].types_to_skip, 1)

    names = [t._clang_type.spelling for t in types]
    self.assertSameElements(['data_s', 's'], names)

    # Extra check for generation, in case rendering throws error for this test.
    generator.generate('Test', [], 'sapi::Tests', None, None)

  def testStructureRelatedTypes(self):
    body = """
      typedef unsigned int uint;

      typedef struct {
        uint a;
        struct {
          int a;
          int b;
        } b;
      } struct_1;

      struct struct_2 {
        uint a;
        char b;
        struct_1* c;
      };

      typedef struct a {
        int b;
      } struct_a;

      extern "C" int function_using_structures(struct struct_2* a1, struct_1* a2,
      struct_a* a3);
    """
    generator = code.Generator([analyze_string(body)])
    functions = generator._get_functions()
    self.assertLen(functions, 1)

    args = functions[0].arguments()
    self.assertLen(args, 3)

    types = args[0].get_related_types()
    names = [t._clang_type.spelling for t in types]
    self.assertLen(types, 4)
    self.assertSameElements(names, [
        'struct_2', 'struct_1::(unnamed struct at tmp.cc:6:9)', 'uint',
        'struct_1'
    ])

    types = args[1].get_related_types()
    names = [t._clang_type.spelling for t in types]
    self.assertLen(types, 3)
    self.assertSameElements(
        names, ['struct_1', 'struct_1::(unnamed struct at tmp.cc:6:9)', 'uint'])

    names = [t._clang_type.spelling for t in generator._get_related_types()]
    self.assertEqual(names, [
        'uint', 'struct_1', 'struct_1::(unnamed struct at tmp.cc:6:9)',
        'struct_2', 'struct_a'
    ])

    types = args[2].get_related_types()
    self.assertLen(types, 1)

    # Extra check for generation, in case rendering throws error for this test.
    generator.generate('Test', [], 'sapi::Tests', None, None)

  def testUnionRelatedTypes(self):
    body = """
      typedef unsigned int uint;

      typedef union {
        uint a;
        union {
          int a;
          int b;
        } b;
      } union_1;

      union union_2 {
        uint a;
        char b;
        union_1* c;
      };

      extern "C" int function_using_unions(union union_2* a1, union_1* a2);
    """
    generator = code.Generator([analyze_string(body)])
    functions = generator._get_functions()
    self.assertLen(functions, 1)

    args = functions[0].arguments()
    self.assertLen(args, 2)

    types = args[0].get_related_types()
    names = [t._clang_type.spelling for t in types]
    self.assertLen(types, 4)
    self.assertSameElements(names, [
        'union_2', 'union_1::(unnamed union at tmp.cc:6:9)', 'uint', 'union_1'
    ])

    types = args[1].get_related_types()
    names = [t._clang_type.spelling for t in types]
    self.assertLen(types, 3)
    self.assertSameElements(
        names, ['union_1', 'union_1::(unnamed union at tmp.cc:6:9)', 'uint'])

    # Extra check for generation, in case rendering throws error for this test.
    generator.generate('Test', [], 'sapi::Tests', None, None)

  def testFunctionPointerRelatedTypes(self):
    body = """
      typedef unsigned int uint;
      typedef unsigned char uchar;
      typedef uint (*funcp)(uchar, uchar);

      struct struct_1 {
        uint (*func)(uchar);
        int a;
      };

      extern "C" void function(struct struct_1* a1, funcp a2);
    """
    generator = code.Generator([analyze_string(body)])
    functions = generator._get_functions()
    self.assertLen(functions, 1)

    args = functions[0].arguments()
    self.assertLen(args, 2)

    types = args[0].get_related_types()
    names = [t._clang_type.spelling for t in types]
    self.assertLen(types, 3)
    self.assertSameElements(names, ['struct_1', 'uint', 'uchar'])

    types = args[1].get_related_types()
    names = [t._clang_type.spelling for t in types]
    self.assertLen(types, 3)
    self.assertSameElements(names, ['funcp', 'uint', 'uchar'])

    # Extra check for generation, in case rendering throws error for this test.
    generator.generate('Test', [], 'sapi::Tests', None, None)

  def testForwardDeclaration(self):
    body = """
      struct struct_6_def;
      typedef struct struct_6_def struct_6;
      typedef struct_6* struct_6p;
      typedef void (*function_p3)(struct_6p);
      struct struct_6_def {
        function_p3 fn;
      };

      extern "C" void function_using_type_loop(struct_6p a1);
    """
    generator = code.Generator([analyze_string(body)])
    functions = generator._get_functions()
    self.assertLen(functions, 1)

    args = functions[0].arguments()
    self.assertLen(args, 1)

    types = args[0].get_related_types()
    names = [t._clang_type.spelling for t in types]
    self.assertLen(types, 4)
    self.assertSameElements(
        names, ['struct_6p', 'struct_6', 'struct_6_def', 'function_p3'])

    self.assertLen(generator.translation_units, 1)
    self.assertLen(generator.translation_units[0].forward_decls, 1)

    t = next(
        x for x in types if x._clang_type.spelling == 'struct_6_def')
    self.assertIn(t, generator.translation_units[0].forward_decls)

    names = [t._clang_type.spelling for t in generator._get_related_types()]
    self.assertEqual(
        names, ['struct_6', 'struct_6p', 'function_p3', 'struct_6_def'])

    # Extra check for generation, in case rendering throws error for this test.
    forward_decls = generator._get_forward_decls(generator._get_related_types())
    self.assertLen(forward_decls, 1)
    self.assertEqual(forward_decls[0], 'struct struct_6_def;')
    generator.generate('Test', [], 'sapi::Tests', None, None)

  def testEnumRelatedTypes(self):
    body = """
      enum Enumeration { ONE, TWO, THREE };
      typedef enum Numbers { UNKNOWN, FIVE = 5, SE7EN = 7 } Nums;
      typedef enum { SIX = 6, TEN = 10 } SixOrTen;
      enum class Color : long long { RED, GREEN = 20, BLUE };  // NOLINT
      enum struct Direction { LEFT = 'l', RIGHT = 'r' };
      enum __rlimit_resource {  RLIMIT_CPU = 0, RLIMIT_MEM = 1};

      extern "C" int function_using_enums(Enumeration a1, SixOrTen a2, Color a3,
                           Direction a4, Nums a5, enum __rlimit_resource a6);
     """
    generator = code.Generator([analyze_string(body)])
    functions = generator._get_functions()
    self.assertLen(functions, 1)

    args = functions[0].arguments()
    self.assertLen(args, 6)

    self.assertLen(args[0].get_related_types(), 1)
    self.assertLen(args[1].get_related_types(), 1)
    self.assertLen(args[2].get_related_types(), 1)
    self.assertLen(args[3].get_related_types(), 1)
    self.assertLen(args[4].get_related_types(), 1)
    self.assertLen(args[5].get_related_types(), 1)

    # Extra check for generation, in case rendering throws error for this test.
    generator.generate('Test', [], 'sapi::Tests', None, None)

  def testArrayAsParam(self):
    body = """
      extern "C" int function_using_enums(char a[10], char *const __argv[]);
     """
    generator = code.Generator([analyze_string(body)])
    functions = generator._get_functions()
    self.assertLen(functions, 1)

    args = functions[0].arguments()
    self.assertLen(args, 2)

  @parameterized.named_parameters(
      ('uint < ushort  ', 'assertLess', 1, 2),
      ('uint < chr     ', 'assertLess', 1, 3),
      ('uint < uchar   ', 'assertLess', 1, 4),
      ('uint < u32     ', 'assertLess', 1, 5),
      ('uint < ulong   ', 'assertLess', 1, 6),
      ('ushort < chr   ', 'assertLess', 2, 3),
      ('ushort < uchar ', 'assertLess', 2, 4),
      ('ushort < u32   ', 'assertLess', 2, 5),
      ('ushort < ulong ', 'assertLess', 2, 6),
      ('chr < uchar    ', 'assertLess', 3, 4),
      ('chr < u32      ', 'assertLess', 3, 5),
      ('chr < ulong    ', 'assertLess', 3, 6),
      ('uchar < u32    ', 'assertLess', 4, 5),
      ('uchar < ulong  ', 'assertLess', 4, 6),
      ('u32 < ulong    ', 'assertLess', 5, 6),
      ('ushort > uint  ', 'assertGreater', 2, 1),
      ('chr > uint     ', 'assertGreater', 3, 1),
      ('uchar > uint   ', 'assertGreater', 4, 1),
      ('u32 > uint     ', 'assertGreater', 5, 1),
      ('ulong > uint   ', 'assertGreater', 6, 1),
      ('chr > ushort   ', 'assertGreater', 3, 2),
      ('uchar > ushort ', 'assertGreater', 4, 2),
      ('u32 > ushort   ', 'assertGreater', 5, 2),
      ('ulong > ushort ', 'assertGreater', 6, 2),
      ('uchar > chr    ', 'assertGreater', 4, 3),
      ('u32 > chr      ', 'assertGreater', 5, 3),
      ('ulong > chr    ', 'assertGreater', 6, 3),
      ('u32 > uchar    ', 'assertGreater', 5, 4),
      ('ulong > uchar  ', 'assertGreater', 6, 4),
      ('ulong > u32    ', 'assertGreater', 6, 5),
  )
  def testTypeOrder(self, func, a1, a2):
    """Checks if comparison functions of Type class work properly.

    This is necessary for Generator._get_related_types to return types in
    proper order, ready to be emitted in the generated file. To be more
    specific: emitted types will be ordered in a way that would allow
    compilation ie. if structure field type is a typedef, typedef definition
    will end up before structure definition.

    Args:
      func: comparison assert to call
      a1: function argument number to take the type to compare
      a2: function argument number to take the type to compare
    """

    file1_code = """
    typedef unsigned int uint;
    #include "/f2.h"
    typedef uint u32;
    #include "/f3.h"

    struct args {
      u32 a;
      uchar b;
      ulong c;
      ushort d;
      chr e;
    };
    extern "C" int function(struct args* a0, uint a1, ushort a2, chr a3,
                 uchar a4, u32 a5, ulong a6, struct args* a7);
    """
    file2_code = """
    typedef unsigned short ushort;
    #include "/f4.h"
    typedef unsigned char uchar;"""
    file3_code = 'typedef unsigned long ulong;'
    file4_code = 'typedef char chr;'
    files = [('f1.h', file1_code), ('/f2.h', file2_code), ('/f3.h', file3_code),
             ('/f4.h', file4_code)]
    generator = code.Generator([analyze_strings('f1.h', files)])
    functions = generator._get_functions()
    self.assertLen(functions, 1)

    args = functions[0].arguments()
    getattr(self, func)(args[a1], args[a2])
    # Extra check for generation, in case rendering throws error for this test.
    generator.generate('Test', [], 'sapi::Tests', None, None)

  def testFilterFunctionsFromInputFilesOnly(self):
    file1_code = """
      #include "/f2.h"

      extern "C" int function1();
    """
    file2_code = """
      extern "C" int function2();
    """

    files = [('f1.h', file1_code), ('/f2.h', file2_code)]
    generator = code.Generator([analyze_strings('f1.h', files)])
    functions = generator._get_functions()
    self.assertLen(functions, 2)

    generator = code.Generator([analyze_strings('f1.h', files, True)])
    functions = generator._get_functions()
    self.assertLen(functions, 1)

  def testTypeToString(self):
    body = """
      #define SIZE 1024
      typedef unsigned int uint;

      typedef struct {
      #if SOME_DEFINE >= 12 \
      && SOME_OTHER == 13
        uint a;
      #else
        uint aa;
      #endif
        struct {
          uint a;
          int b;
          char c[SIZE];
        } b;
      } struct_1;

      extern "C" int function_using_structures(struct_1* a1);
    """

    # pylint: disable=trailing-whitespace
    expected = """typedef struct {
#if SOME_DEFINE >= 12 && SOME_OTHER == 13
\tuint a ;
#else
\tuint aa ;
#endif
\tstruct {
\t\tuint a ;
\t\tint b ;
\t\tchar c [ SIZE ] ;
\t} b ;
} struct_1"""
    generator = code.Generator([analyze_string(body)])
    functions = generator._get_functions()
    self.assertLen(functions, 1)

    types = generator._get_related_types()
    self.assertLen(types, 3)
    self.assertEqual('typedef unsigned int uint', types[0].stringify())
    self.assertMultiLineEqual(expected, types[1].stringify())

    # Extra check for generation, in case rendering throws error for this test.
    generator.generate('Test', [], 'sapi::Tests', None, None)

  def testCollectDefines(self):
    body = """
      #define SIZE 1024
      #define NOT_USED 7
      #define SIZE2 2*1024
      #define SIZE3 1337
      #define SIZE4 10
      struct test {
        int a[SIZE];
        char b[SIZE2];
        float c[777];
        int (*d)[SIZE3*SIZE4];
      };
      extern "C" int function_1(struct test* a1);
    """
    generator = code.Generator([analyze_string(body)])
    self.assertLen(generator.translation_units, 1)

    generator._get_related_types()
    tu = generator.translation_units[0]
    tu._process()

    self.assertLen(tu.required_defines, 4)
    defines = generator._get_defines()
    self.assertLen(defines, 4)
    self.assertIn('#define SIZE 1024', defines)
    self.assertIn('#define SIZE2 2 * 1024', defines)
    self.assertIn('#define SIZE3 1337', defines)
    self.assertIn('#define SIZE4 10', defines)

    # Extra check for generation, in case rendering throws error for this test.
    generator.generate('Test', [], 'sapi::Tests', None, None)

  def testYaraCase(self):
    body = """
      #define YR_ALIGN(n) __attribute__((aligned(n)))
      #define DECLARE_REFERENCE(type, name) union {    \
        type name;            \
        int64_t name##_;      \
      } YR_ALIGN(8)
      struct YR_NAMESPACE {
        int32_t t_flags[1337];
        DECLARE_REFERENCE(char*, name);
      };

      extern "C" int function_1(struct YR_NAMESPACE* a1);
    """
    generator = code.Generator([analyze_string(body)])
    self.assertLen(generator.translation_units, 1)

    generator._get_related_types()
    tu = generator.translation_units[0]
    tu._process()

    self.assertLen(tu.required_defines, 2)
    defines = generator._get_defines()
    # _get_defines will add dependant defines to tu.required_defines
    self.assertLen(defines, 2)
    gold = '#define DECLARE_REFERENCE('
    # DECLARE_REFERENCE must be second to pass this test
    self.assertTrue(defines[1].startswith(gold))

    # Extra check for generation, in case rendering throws error for this test.
    generator.generate('Test', [], 'sapi::Tests', None, None)

  def testDoubleFunction(self):
    body = """
      extern "C" int function_1(int a);
      extern "C" int function_1(int a) {
        return a + 1;
      };
    """
    generator = code.Generator([analyze_string(body)])
    self.assertLen(generator.translation_units, 1)

    tu = generator.translation_units[0]
    tu._process()

    self.assertLen(tu.functions, 1)

    # Extra check for generation, in case rendering throws error for this test.
    generator.generate('Test', [], 'sapi::Tests', None, None)

  def testDefineStructBody(self):
    body = """
      #define STRUCT_BODY \
      int a;  \
      char b; \
      long c
      struct test {
        STRUCT_BODY;
      };
      extern "C" void function(struct test* a1);
    """

    generator = code.Generator([analyze_string(body)])
    self.assertLen(generator.translation_units, 1)

    # initialize all internal data
    generator.generate('Test', [], 'sapi::Tests', None, None)
    tu = generator.translation_units[0]

    self.assertLen(tu.functions, 1)
    self.assertLen(tu.required_defines, 1)

  def testJpegTurboCase(self):
    body = """
      typedef short JCOEF;
      #define DCTSIZE2 1024
      typedef JCOEF JBLOCK[DCTSIZE2];

      extern "C" void function(JBLOCK* a);
    """
    generator = code.Generator([analyze_string(body)])
    self.assertLen(generator.translation_units, 1)

    # initialize all internal data
    generator.generate('Test', [], 'sapi::Tests', None, None)

    tu = generator.translation_units[0]
    self.assertLen(tu.functions, 1)
    self.assertLen(generator._get_defines(), 1)
    self.assertLen(generator._get_related_types(), 2)

  def testMultipleTypesWhenConst(self):
    body = """
      struct Instance {
        void* instance = nullptr;
        void* state_memory = nullptr;
        void* scratch_memory = nullptr;
      };

      extern "C" void function1(Instance* a);
      extern "C" void function2(const Instance* a);
    """
    generator = code.Generator([analyze_string(body)])
    self.assertLen(generator.translation_units, 1)

    # Initialize all internal data
    generator.generate('Test', [], 'sapi::Tests', None, None)

    tu = generator.translation_units[0]
    self.assertLen(tu.functions, 2)
    self.assertLen(generator._get_related_types(), 1)

  def testReference(self):
    body = """
      struct Instance {
        int a;
      };

      void Function1(Instance& a, Instance&& a);
    """
    generator = code.Generator([analyze_string(body)])
    self.assertLen(generator.translation_units, 1)

    # Initialize all internal data
    generator.generate('Test', [], 'sapi::Tests', None, None)

    tu = generator.translation_units[0]
    self.assertLen(tu.functions, 1)

    # this will return 0 related types because function will be mangled and
    # filtered out by generator
    self.assertEmpty(generator._get_related_types())
    self.assertLen(next(iter(tu.functions)).get_related_types(), 1)

  def testCppHeader(self):
    path = 'tmp.h'
    content = """
      int sum(int a, float b);

      extern "C" int sum(int a, float b);
    """
    unsaved_files = [(path, content)]
    generator = code.Generator([analyze_strings(path, unsaved_files)])
    # Initialize all internal data
    generator.generate('Test', [], 'sapi::Tests', None, None)

    # generator should filter out mangled function
    functions = generator._get_functions()
    self.assertLen(functions, 1)

    tu = generator.translation_units[0]
    functions = tu.get_functions()
    self.assertLen(functions, 2)

    mangled_names = [f.cursor.mangled_name for f in functions]
    self.assertSameElements(mangled_names, ['sum', '_Z3sumif'])


if __name__ == '__main__':
  absltest.main()
