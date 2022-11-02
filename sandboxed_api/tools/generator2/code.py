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
"""Module related to code analysis and generation."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from ctypes import util
import itertools
import os
# pylint: disable=unused-import
from typing import (Text, List, Optional, Set, Dict, Callable, IO,
                    Generator as Gen, Tuple, Union, Sequence)  # pyformat: disable
# pylint: enable=unused-import
from clang import cindex


_PARSE_OPTIONS = (
    cindex.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES
    | cindex.TranslationUnit.PARSE_INCOMPLETE |
    # for include directives
    cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD)


def _init_libclang():
  """Finds and initializes the libclang library."""
  if cindex.Config.loaded:
    return
  # Try to find libclang in the standard location and a few versioned paths
  # that are used on Debian (and others). If LD_LIBRARY_PATH is set, it is
  # used as well.
  for version in ['', '12', '11', '10', '9', '8', '7', '6.0', '5.0', '4.0']:
    libname = 'clang' + ('-' + version if version else '')
    libclang = util.find_library(libname)
    if libclang:
      cindex.Config.set_library_file(libclang)
      break


def get_header_guard(path):
  # type: (Text) -> Text
  """Generates header guard string from path."""
  # the output file will be most likely somewhere in genfiles, strip the
  # prefix in that case, also strip .gen if this is a step before clang-format
  if not path:
    raise ValueError('Cannot prepare header guard from path: {}'.format(path))
  if 'genfiles/' in path:
    path = path.split('genfiles/')[1]
  if path.endswith('.gen'):
    path = path.split('.gen')[0]
  path = path.upper().replace('.', '_').replace('-', '_').replace('/', '_')
  return path + '_'


def _stringify_tokens(tokens, separator='\n'):
  # type: (Sequence[cindex.Token], Text) -> Text
  """Converts tokens to text respecting line position (disrespecting column)."""
  previous = OutputLine(0, [])  # not used in output
  lines = []  # type: List[OutputLine]

  for _, group in itertools.groupby(tokens, lambda t: t.location.line):
    group_list = list(group)
    line = OutputLine(previous.next_tab, group_list)

    lines.append(line)
    previous = line

  return separator.join(str(l) for l in lines)


TYPE_MAPPING = {
    cindex.TypeKind.VOID: '::sapi::v::Void',
    cindex.TypeKind.CHAR_S: '::sapi::v::Char',
    cindex.TypeKind.CHAR_U: '::sapi::v::Char',
    cindex.TypeKind.INT: '::sapi::v::Int',
    cindex.TypeKind.UINT: '::sapi::v::UInt',
    cindex.TypeKind.LONG: '::sapi::v::Long',
    cindex.TypeKind.ULONG: '::sapi::v::ULong',
    cindex.TypeKind.UCHAR: '::sapi::v::UChar',
    cindex.TypeKind.USHORT: '::sapi::v::UShort',
    cindex.TypeKind.SHORT: '::sapi::v::Short',
    cindex.TypeKind.LONGLONG: '::sapi::v::LLong',
    cindex.TypeKind.ULONGLONG: '::sapi::v::ULLong',
    cindex.TypeKind.FLOAT: '::sapi::v::Reg<float>',
    cindex.TypeKind.DOUBLE: '::sapi::v::Reg<double>',
    cindex.TypeKind.LONGDOUBLE: '::sapi::v::Reg<long double>',
    cindex.TypeKind.SCHAR: '::sapi::v::SChar',
    cindex.TypeKind.SHORT: '::sapi::v::Short',
    cindex.TypeKind.BOOL: '::sapi::v::Bool',
}


class Type(object):
  """Class representing a type.

  Wraps cindex.Type of the argument/return value and provides helpers for the
  code generation.
  """

  def __init__(self, tu, clang_type):
    # type: (_TranslationUnit, cindex.Type) -> None
    self._clang_type = clang_type
    self._tu = tu

  # pylint: disable=protected-access
  def __eq__(self, other):
    # type: (Type) -> bool
    # Use get_usr() to deduplicate Type objects based on declaration
    decl = self._get_declaration()
    decl_o = other._get_declaration()

    return decl.get_usr() == decl_o.get_usr()

  def __ne__(self, other):
    # type: (Type) -> bool
    return not self.__eq__(other)

  def __lt__(self, other):
    # type: (Type) -> bool
    """Compares two Types belonging to the same TranslationUnit.

    This is being used to properly order types before emitting to generated
    file. To be more specific: structure definition that contains field that is
    a typedef should end up after that typedef definition. This is achieved by
    exploiting the order in which clang iterate over AST in translation unit.

    Args:
      other: other comparison type

    Returns:
      true if this Type occurs earlier in the AST than 'other'
    """
    self._validate_tu(other)
    return (self._tu.order[self._get_declaration().hash] <
            self._tu.order[other._get_declaration().hash])  # pylint: disable=protected-access

  def __gt__(self, other):
    # type: (Type) -> bool
    """Compares two Types belonging to the same TranslationUnit.

    This is being used to properly order types before emitting to generated
    file. To be more specific: structure definition that contains field that is
    a typedef should end up after that typedef definition. This is achieved by
    exploiting the order in which clang iterate over AST in translation unit.

    Args:
      other: other comparison type

    Returns:
      true if this Type occurs later in the AST than 'other'
    """
    self._validate_tu(other)
    return (self._tu.order[self._get_declaration().hash] >
            self._tu.order[other._get_declaration().hash])  # pylint: disable=protected-access

  def __hash__(self):
    """Types with the same declaration should hash to the same value."""
    return hash(self._get_declaration().get_usr())

  def _validate_tu(self, other):
    # type: (Type) -> None
    if self._tu != other._tu:  # pylint: disable=protected-access
      raise ValueError('Cannot compare types from different translation units.')

  def is_void(self):
    # type: () -> bool
    return self._clang_type.kind == cindex.TypeKind.VOID

  def is_typedef(self):
    # type: () -> bool
    return self._clang_type.kind == cindex.TypeKind.TYPEDEF

  def is_elaborated(self):
    # type: () -> bool
    return self._clang_type.kind == cindex.TypeKind.ELABORATED

  # Hack: both class and struct types are indistinguishable except for
  # declaration cursor kind
  def is_sugared_record(self):  # class, struct, union
    # type: () -> bool
    return self._clang_type.get_declaration().kind in (
        cindex.CursorKind.STRUCT_DECL, cindex.CursorKind.UNION_DECL,
        cindex.CursorKind.CLASS_DECL)

  def is_struct(self):
    # type: () -> bool
    return (self._clang_type.get_declaration().kind ==
            cindex.CursorKind.STRUCT_DECL)

  def is_class(self):
    # type: () -> bool
    return (self._clang_type.get_declaration().kind ==
            cindex.CursorKind.CLASS_DECL)

  def is_union(self):
    # type: () -> bool
    return (self._clang_type.get_declaration().kind ==
            cindex.CursorKind.UNION_DECL)

  def is_function(self):
    # type: () -> bool
    return self._clang_type.kind == cindex.TypeKind.FUNCTIONPROTO

  def is_sugared_ptr(self):
    # type: () -> bool
    return self._clang_type.get_canonical().kind == cindex.TypeKind.POINTER

  def is_sugared_enum(self):
    # type: () -> bool
    return self._clang_type.get_canonical().kind == cindex.TypeKind.ENUM

  def is_const_array(self):
    # type: () -> bool
    return self._clang_type.kind == cindex.TypeKind.CONSTANTARRAY

  def is_simple_type(self):
    # type: () -> bool
    return self._clang_type.kind in TYPE_MAPPING

  def get_pointee(self):
    # type: () -> Type
    return Type(self._tu, self._clang_type.get_pointee())

  def _get_declaration(self):
    # type: () -> cindex.Cursor
    decl = self._clang_type.get_declaration()
    if decl.kind == cindex.CursorKind.NO_DECL_FOUND and self.is_sugared_ptr():
      decl = self.get_pointee()._get_declaration()  # pylint: disable=protected-access

    return decl

  def get_related_types(self, result=None, skip_self=False):
    # type: (Optional[Set[Type]], bool) -> Set[Type]
    """Returns all types related to this one eg. typedefs, nested structs."""
    if result is None:
      result = set()

    # Base case.
    if self in result or self.is_simple_type() or self.is_class():
      return result

    # Sugar types.
    if self.is_typedef():
      return self._get_related_types_of_typedef(result)

    if self.is_elaborated():
      return Type(self._tu,
                  self._clang_type.get_named_type()).get_related_types(
                      result, skip_self)

    # Composite types.
    if self.is_const_array():
      t = Type(self._tu, self._clang_type.get_array_element_type())
      return t.get_related_types(result)

    if self._clang_type.kind in (cindex.TypeKind.POINTER,
                                 cindex.TypeKind.MEMBERPOINTER,
                                 cindex.TypeKind.LVALUEREFERENCE,
                                 cindex.TypeKind.RVALUEREFERENCE):
      return self.get_pointee().get_related_types(result, skip_self)

    # union + struct, class should be filtered out
    if self.is_struct() or self.is_union():
      return self._get_related_types_of_record(result, skip_self)

    if self.is_function():
      return self._get_related_types_of_function(result)

    if self.is_sugared_enum():
      if not skip_self:
        result.add(self)
        self._tu.search_for_macro_name(self._get_declaration())
      return result

    # Ignore all cindex.TypeKind.UNEXPOSED AST nodes
    # TODO(b/256934562): Remove the disable once the pytype bug is fixed.
    return result  # pytype: disable=bad-return-type

  def _get_related_types_of_typedef(self, result):
    # type: (Set[Type]) -> Set[Type]
    """Returns all intermediate types related to the typedef."""
    result.add(self)
    decl = self._clang_type.get_declaration()
    self._tu.search_for_macro_name(decl)

    t = Type(self._tu, decl.underlying_typedef_type)
    if t.is_sugared_ptr():
      t = t.get_pointee()

    if not t.is_simple_type():
      skip_child = self.contains_declaration(t)
      if t.is_sugared_record() and skip_child:
        # if child declaration is contained in parent, we don't have to emit it
        self._tu.types_to_skip.add(t)
      result.update(t.get_related_types(result, skip_child))

    return result

  def _get_related_types_of_record(self, result, skip_self=False):
    # type: (Set[Type], bool) -> Set[Type]
    """Returns all types related to the structure."""
    # skip unnamed structures eg. typedef struct {...} x;
    # struct {...} will be rendered as part of typedef rendering
    if self._get_declaration().spelling and not skip_self:
      self._tu.search_for_macro_name(self._get_declaration())
      result.add(self)

    for f in self._clang_type.get_fields():
      self._tu.search_for_macro_name(f)
      result.update(Type(self._tu, f.type).get_related_types(result))

    return result

  def _get_related_types_of_function(self, result):
    # type: (Set[Type]) -> Set[Type]
    """Returns all types related to the function."""
    for arg in self._clang_type.argument_types():
      result.update(Type(self._tu, arg).get_related_types(result))
    related = Type(self._tu,
                   self._clang_type.get_result()).get_related_types(result)
    result.update(related)

    return result

  def contains_declaration(self, other):
    # type: (Type) -> bool
    """Checks if string representation of a type contains the other type."""
    self_extent = self._get_declaration().extent
    other_extent = other._get_declaration().extent  # pylint: disable=protected-access

    if other_extent.start.file is None:
      return False
    return (other_extent.start in self_extent and
            other_extent.end in self_extent)

  def stringify(self):
    # type: () -> Text
    """Returns string representation of the Type."""
    # (szwl): as simple as possible, keeps macros in separate lines not to
    # break things; this will go through clang format nevertheless
    tokens = [
        x for x in self._get_declaration().get_tokens()
        if x.kind is not cindex.TokenKind.COMMENT
    ]

    return _stringify_tokens(tokens)


class OutputLine(object):
  """Helper class for Type printing."""

  def __init__(self, tab, tokens):
    # type: (int, List[cindex.Token]) -> None
    self.tokens = tokens
    self.spellings = []
    self.define = False
    self.tab = tab
    self.next_tab = tab
    list(map(self._process_token, self.tokens))

  def _process_token(self, t):
    # type: (cindex.Token) -> None
    """Processes a token, setting up internal states rel. to intendation."""
    if t.spelling == '#':
      self.define = True
    elif t.spelling == '{':
      self.next_tab += 1
    elif t.spelling == '}':
      self.tab -= 1
      self.next_tab -= 1

    is_bracket = t.spelling == '('
    is_macro = len(self.spellings) == 1 and self.spellings[0] == '#'
    if self.spellings and not is_bracket and not is_macro:
      self.spellings.append(' ')
    self.spellings.append(t.spelling)

  def __str__(self):
    # type: () -> Text
    tabs = ('\t' * self.tab) if not self.define else ''
    return tabs + ''.join(t for t in self.spellings)


class ArgumentType(Type):
  """Class representing function argument type.

  Object fields are being used by the code template:
  pos: argument position
  type: string representation of the type
  argument: string representation of the type as function argument
  mapped_type: SAPI equivalent of the type
  wrapped: wraps type in SAPI object constructor
  call_argument: type (or it's sapi wrapper) used in function call
  """

  def __init__(self, function, pos, arg_type, name=None):
    # type: (Function, int, cindex.Type, Optional[Text]) -> None
    super(ArgumentType, self).__init__(function.translation_unit(), arg_type)
    self._function = function

    self.pos = pos
    self.name = name or 'a{}'.format(pos)
    self.type = arg_type.spelling

    template = '{}' if self.is_sugared_ptr() else '&{}_'
    self.call_argument = template.format(self.name)

  def __str__(self):
    # type: () -> Text
    """Returns function argument prepared from the type."""
    if self.is_sugared_ptr():
      return '::sapi::v::Ptr* {}'.format(self.name)

    return '{} {}'.format(self._clang_type.spelling, self.name)

  @property
  def wrapped(self):
    # type: () -> Text
    return '{} {name}_(({name}))'.format(self.mapped_type, name=self.name)

  @property
  def mapped_type(self):
    # type: () -> Text
    """Maps the type to its SAPI equivalent."""
    if self.is_sugared_ptr():
      # TODO(szwl): const ptrs do not play well with SAPI C++ API...
      spelling = self._clang_type.spelling.replace('const', '')
      return '::sapi::v::Reg<{}>'.format(spelling)

    type_ = self._clang_type

    if type_.kind == cindex.TypeKind.TYPEDEF:
      type_ = self._clang_type.get_canonical()
    if type_.kind == cindex.TypeKind.ELABORATED:
      type_ = type_.get_canonical()
    if type_.kind == cindex.TypeKind.ENUM:
      return '::sapi::v::IntBase<{}>'.format(self._clang_type.spelling)
    if type_.kind in [
        cindex.TypeKind.CONSTANTARRAY, cindex.TypeKind.INCOMPLETEARRAY
    ]:
      return '::sapi::v::Reg<{}>'.format(self._clang_type.spelling)

    if type_.kind == cindex.TypeKind.LVALUEREFERENCE:
      return 'LVALUEREFERENCE::NOT_SUPPORTED'

    if type_.kind == cindex.TypeKind.RVALUEREFERENCE:
      return 'RVALUEREFERENCE::NOT_SUPPORTED'

    if type_.kind in [cindex.TypeKind.RECORD, cindex.TypeKind.ELABORATED]:
      raise ValueError('Elaborate type (eg. struct) in mapped_type is not '
                       'supported: function {}, arg {}, type {}, location {}'
                       ''.format(self._function.name, self.pos,
                                 self._clang_type.spelling,
                                 self._function.cursor.location))

    if type_.kind not in TYPE_MAPPING:
      raise KeyError('Key {} does not exist in TYPE_MAPPING.'
                     ' function {}, arg {}, type {}, location {}'
                     ''.format(type_.kind, self._function.name, self.pos,
                               self._clang_type.spelling,
                               self._function.cursor.location))

    return TYPE_MAPPING[type_.kind]


class ReturnType(ArgumentType):
  """Class representing function return type.

     Attributes:
       return_type: absl::StatusOr<T> where T is original return type, or
                    absl::Status for functions returning void
  """

  def __init__(self, function, arg_type):
    # type: (Function, cindex.Type) -> None
    super(ReturnType, self).__init__(function, 0, arg_type, None)

  def __str__(self):
    # type: () -> Text
    """Returns function return type prepared from the type."""
    # TODO(szwl): const ptrs do not play well with SAPI C++ API...
    spelling = self._clang_type.spelling.replace('const', '')
    return_type = 'absl::StatusOr<{}>'.format(spelling)
    return_type = 'absl::Status' if self.is_void() else return_type
    return return_type


class Function(object):
  """Class representing SAPI-wrapped function used by the template.

  Wraps Clang cursor object of kind FUNCTION_DECL and provides helpers to
  aid code generation.
  """

  def __init__(self, tu, cursor):
    # type: (_TranslationUnit, cindex.Cursor) -> None
    self._tu = tu
    self.cursor = cursor  # type: cindex.Index
    self.name = cursor.spelling  # type: Text
    self.result = ReturnType(self, cursor.result_type)
    self.original_definition = '{} {}'.format(
        cursor.result_type.spelling, self.cursor.displayname)  # type: Text

    types = self.cursor.get_arguments()
    self.argument_types = [
        ArgumentType(self, i, t.type, t.spelling) for i, t in enumerate(types)
    ]

  def translation_unit(self):
    # type: () -> _TranslationUnit
    return self._tu

  def arguments(self):
    # type: () -> List[ArgumentType]
    return self.argument_types

  def call_arguments(self):
    # type: () -> List[Text]
    return [a.call_argument for a in self.argument_types]

  def get_absolute_path(self):
    # type: () -> Text
    return self.cursor.location.file.name

  def get_include_path(self, prefix):
    # type: (Optional[Text]) -> Text
    """Creates a proper include path."""
    # TODO(szwl): sanity checks
    # TODO(szwl): prefix 'utils/' and the path is '.../fileutils/...' case
    if prefix and not prefix.endswith('/'):
      prefix += '/'

    if not prefix:
      return self.get_absolute_path()
    elif prefix in self.get_absolute_path():
      return prefix + self.get_absolute_path().split(prefix)[-1]
    return prefix + self.get_absolute_path().split('/')[-1]

  def get_related_types(self, processed=None):
    # type: (Optional[Set[Type]]) -> Set[Type]
    result = self.result.get_related_types(processed)
    for a in self.argument_types:
      result.update(a.get_related_types(processed))

    return result

  def is_mangled(self):
    # type: () -> bool
    return self.cursor.mangled_name != self.cursor.spelling

  def __hash__(self):
    # type: () -> int
    return hash(self.cursor.get_usr())

  def __eq__(self, other):
    # type: (Function) -> bool
    return self.cursor.mangled_name == other.cursor.mangled_name


class _TranslationUnit(object):
  """Class wrapping clang's _TranslationUnit. Provides extra utilities."""

  def __init__(self, path, tu, limit_scan_depth=False):
    # type: (Text, cindex.TranslationUnit, bool) -> None
    self.path = path
    self.limit_scan_depth = limit_scan_depth
    self._tu = tu
    self._processed = False
    self.forward_decls = dict()
    self.functions = set()
    self.order = dict()
    self.defines = {}
    self.required_defines = set()
    self.types_to_skip = set()

  def _process(self):
    # type: () -> None
    """Walks the cursor tree and caches some for future use."""
    if not self._processed:
      # self.includes[self._tu.spelling] = (0, self._tu.cursor)
      self._processed = True
      # TODO(szwl): duplicates?
      # TODO(szwl): for d in translation_unit.diagnostics:, handle that

      for i, cursor in enumerate(self._walk_preorder()):
        # Workaround for issue#32
        # ignore all the cursors with kinds not implemented in python bindings
        try:
          cursor.kind
        except ValueError:
          continue
        # naive way to order types: they should be ordered when walking the tree
        if cursor.kind.is_declaration():
          self.order[cursor.hash] = i

        if (cursor.kind == cindex.CursorKind.MACRO_DEFINITION and
            cursor.location.file):
          self.order[cursor.hash] = i
          self.defines[cursor.spelling] = cursor

        # most likely a forward decl of struct
        if (cursor.kind == cindex.CursorKind.STRUCT_DECL and
            not cursor.is_definition()):
          self.forward_decls[Type(self, cursor.type)] = cursor
        if (cursor.kind == cindex.CursorKind.FUNCTION_DECL and
            cursor.linkage != cindex.LinkageKind.INTERNAL):
          if self.limit_scan_depth:
            if (cursor.location and cursor.location.file.name == self.path):
              self.functions.add(Function(self, cursor))
          else:
            self.functions.add(Function(self, cursor))

  def get_functions(self):
    # type: () -> Set[Function]
    if not self._processed:
      self._process()
    return self.functions

  def _walk_preorder(self):
    # type: () -> Gen
    for c in self._tu.cursor.walk_preorder():
      yield c

  def search_for_macro_name(self, cursor):
    # type: (cindex.Cursor) -> None
    """Searches for possible macro usage in constant array types."""
    tokens = list(t.spelling for t in cursor.get_tokens())
    try:
      for token in tokens:
        if token in self.defines and token not in self.required_defines:
          self.required_defines.add(token)
          self.search_for_macro_name(self.defines[token])
    except ValueError:
      return


class Analyzer(object):
  """Class responsible for analysis."""

  @staticmethod
  def process_files(input_paths, compile_flags, limit_scan_depth=False):
    # type: (Text, List[Text], bool) -> List[_TranslationUnit]
    """Processes files with libclang and returns TranslationUnit objects."""
    _init_libclang()

    tus = []
    for path in input_paths:
      tu = Analyzer._analyze_file_for_tu(
          path, compile_flags=compile_flags, limit_scan_depth=limit_scan_depth)
      tus.append(tu)
    return tus

  # pylint: disable=line-too-long
  @staticmethod
  def _analyze_file_for_tu(path,
                           compile_flags=None,
                           test_file_existence=True,
                           unsaved_files=None,
                           limit_scan_depth=False):
    # type: (Text, Optional[List[Text]], bool, Optional[Tuple[Text, Union[Text, IO[Text]]]], bool) -> _TranslationUnit
    """Returns Analysis object for given path."""
    compile_flags = compile_flags or []
    if test_file_existence and not os.path.isfile(path):
      raise IOError('Path {} does not exist.'.format(path))

    _init_libclang()
    index = cindex.Index.create()  # type: cindex.Index
    # TODO(szwl): hack until I figure out how python swig does that.
    # Headers will be parsed as C++. C libs usually have
    # '#ifdef __cplusplus extern "C"' for compatibility with c++
    lang = '-xc++' if not path.endswith('.c') else '-xc'
    args = [lang]
    args += compile_flags
    args.append('-I.')
    return _TranslationUnit(
        path,
        index.parse(
            path,
            args=args,
            unsaved_files=unsaved_files,
            options=_PARSE_OPTIONS),
        limit_scan_depth=limit_scan_depth)


class Generator(object):
  """Class responsible for code generation."""

  AUTO_GENERATED = ('// AUTO-GENERATED by the Sandboxed API generator.\n'
                    '// Edits will be discarded when regenerating this file.\n')

  GUARD_START = ('#ifndef {0}\n' '#define {0}')
  GUARD_END = '#endif  // {}'
  EMBED_INCLUDE = '#include "{}"'
  EMBED_CLASS = ('class {0}Sandbox : public ::sapi::Sandbox {{\n'
                 ' public:\n'
                 '  {0}Sandbox() : ::sapi::Sandbox({1}_embed_create()) {{}}\n'
                 '}};')

  def __init__(self, translation_units):
    # type: (List[cindex.TranslationUnit]) -> None
    """Initializes the generator.

    Args:
      translation_units: list of translation_units for analyzed files,
        facultative. If not given, then one is computed for each element of
        input_paths
    """
    self.translation_units = translation_units
    self.functions = None
    _init_libclang()

  def generate(self,
               name,
               function_names,
               namespace=None,
               output_file=None,
               embed_dir=None,
               embed_name=None):
    # pylint: disable=line-too-long
    # type: (Text, List[Text], Optional[Text], Optional[Text], Optional[Text], Optional[Text]) -> Text
    """Generates structures, functions and typedefs.

    Args:
      name: name of the class that will contain generated interface
      function_names: list of function names to export to the interface
      namespace: namespace of the interface
      output_file: path to the output file, used to generate header guards;
        defaults to None that does not generate the guard #include directives;
        defaults to None that causes to emit the whole file path
      embed_dir: path to directory with embed includes
      embed_name: name of the embed object

    Returns:
      generated interface as a string
    """
    related_types = self._get_related_types(function_names)
    forward_decls = self._get_forward_decls(related_types)
    functions = self._get_functions(function_names)
    related_types = [(t.stringify() + ';') for t in related_types]
    defines = self._get_defines()

    api = {
        'name': name,
        'functions': functions,
        'related_types': defines + forward_decls + related_types,
        'namespaces': namespace.split('::') if namespace else [],
        'embed_dir': embed_dir,
        'embed_name': embed_name,
        'output_file': output_file
    }
    return self.format_template(**api)

  def _get_functions(self, func_names=None):
    # type: (Optional[List[Text]]) -> List[Function]
    """Gets Function objects that will be used to generate interface."""
    if self.functions is not None:
      return self.functions
    self.functions = []
    # TODO(szwl): for d in translation_unit.diagnostics:, handle that
    for translation_unit in self.translation_units:
      self.functions += [
          f for f in translation_unit.get_functions()
          if not func_names or f.name in func_names
      ]
    # allow only nonmangled functions - C++ overloads are not handled in
    # code generation
    self.functions = [f for f in self.functions if not f.is_mangled()]

    # remove duplicates
    self.functions = list(set(self.functions))
    self.functions.sort(key=lambda x: x.name)
    return self.functions

  def _get_related_types(self, func_names=None):
    # type: (Optional[List[Text]]) -> List[Type]
    """Gets type definitions related to chosen functions.

    Types related to one function will land in the same translation unit,
    we gather the types, sort it and put as a sublist in types list.
    This is necessary as we can't compare types from two different translation
    units.

    Args:
      func_names: list of function names to take into consideration, empty means
        all functions.

    Returns:
      list of types in correct (ready to render) order
    """
    processed = set()
    fn_related_types = set()
    types = []
    types_to_skip = set()

    for f in self._get_functions(func_names):
      fn_related_types = f.get_related_types()
      types += sorted(r for r in fn_related_types if r not in processed)
      processed.update(fn_related_types)
      types_to_skip.update(f.translation_unit().types_to_skip)

    return [t for t in types if t not in types_to_skip]

  def _get_defines(self):
    # type: () -> List[Text]
    """Gets #define directives that appeared during TranslationUnit processing.

    Returns:
      list of #define string representations
    """

    def make_sort_condition(translation_unit):
      return lambda cursor: translation_unit.order[cursor.hash]

    result = []
    for tu in self.translation_units:
      tmp_result = []
      sort_condition = make_sort_condition(tu)
      for name in tu.required_defines:
        if name in tu.defines:
          define = tu.defines[name]
          tmp_result.append(define)
      for define in sorted(tmp_result, key=sort_condition):
        result.append('#define ' +
                      _stringify_tokens(define.get_tokens(), separator=' \\\n'))
    return result

  def _get_forward_decls(self, types):
    # type: (List[Type]) -> List[Text]
    """Gets forward declarations of related types, if present."""
    forward_decls = dict()
    result = []
    done = set()
    for tu in self.translation_units:
      forward_decls.update(tu.forward_decls)

      for t in types:
        if t in forward_decls and t not in done:
          result.append(_stringify_tokens(forward_decls[t].get_tokens()) + ';')
          done.add(t)

    return result

  def _format_function(self, f):
    # type: (Function) -> Text
    """Renders one function of the Api.

    Args:
      f: function object with information necessary to emit full function body

    Returns:
      filled function template
    """
    result = []
    result.append('  // {}'.format(f.original_definition))

    arguments = ', '.join(str(a) for a in f.arguments())
    result.append('  {} {}({}) {{'.format(f.result, f.name, arguments))
    result.append('    {} ret;'.format(f.result.mapped_type))

    argument_types = []
    for a in f.argument_types:
      if not a.is_sugared_ptr():
        argument_types.append(a.wrapped + ';')
    if argument_types:
      for arg in argument_types:
        result.append('    {}'.format(arg))

    call_arguments = f.call_arguments()
    if call_arguments:  # fake empty space to add ',' before first argument
      call_arguments.insert(0, '')
    result.append('')
    # For OSS, the macro below will be replaced.
    result.append('    SAPI_RETURN_IF_ERROR(sandbox_->Call("{}", &ret{}));'
                  ''.format(f.name, ', '.join(call_arguments)))

    return_status = 'return absl::OkStatus();'
    if f.result and not f.result.is_void():
      if f.result and f.result.is_sugared_enum():
        return_status = ('return static_cast<{}>'
                         '(ret.GetValue());').format(f.result.type)
      else:
        return_status = 'return ret.GetValue();'
    result.append('    {}'.format(return_status))
    result.append('  }')

    return '\n'.join(result)

  def format_template(self, name, functions, related_types, namespaces,
                      embed_dir, embed_name, output_file):
    # pylint: disable=line-too-long
    # type: (Text, List[Function], List[Type], List[Text], Text, Text, Text) -> Text
    # pylint: enable=line-too-long
    """Formats arguments into proper interface header file.

    Args:
      name: name of the Api - 'Test' will yield TestApi object
      functions: list of functions to generate
      related_types: types used in the above functions
      namespaces: list of namespaces to wrap the Api class with
      embed_dir: directory where the embedded library lives
      embed_name: name of embedded library
      output_file: interface output path - used in header guard generation

    Returns:
      generated header file text
    """
    result = [Generator.AUTO_GENERATED]

    header_guard = get_header_guard(output_file) if output_file else ''
    if header_guard:
      result.append(Generator.GUARD_START.format(header_guard))

    # Copybara transform results in the paths below.
    result.append('#include "absl/status/status.h"')
    result.append('#include "absl/status/statusor.h"')
    result.append('#include "sandboxed_api/sandbox.h"')
    result.append('#include "sandboxed_api/util/status_macros.h"')
    result.append('#include "sandboxed_api/vars.h"')

    if embed_name:
      embed_dir = embed_dir or ''
      result.append(
          Generator.EMBED_INCLUDE.format(
              os.path.join(embed_dir, embed_name) + '_embed.h'))

    if namespaces:
      result.append('')
      for n in namespaces:
        result.append('namespace {} {{'.format(n))

    if related_types:
      result.append('')
      for t in related_types:
        result.append(t)

    result.append('')

    if embed_name:
      result.append(
          Generator.EMBED_CLASS.format(name, embed_name.replace('-', '_')))

    result.append('class {}Api {{'.format(name))
    result.append(' public:')
    result.append('  explicit {}Api(::sapi::Sandbox* sandbox)'
                  ' : sandbox_(sandbox) {{}}'.format(name))
    result.append('  // Deprecated')
    result.append('  ::sapi::Sandbox* GetSandbox() const { return sandbox(); }')
    result.append('  ::sapi::Sandbox* sandbox() const { return sandbox_; }')

    for f in functions:
      result.append('')
      result.append(self._format_function(f))

    result.append('')
    result.append(' private:')
    result.append('  ::sapi::Sandbox* sandbox_;')
    result.append('};')
    result.append('')

    if namespaces:
      for n in reversed(namespaces):
        result.append('}}  // namespace {}'.format(n))

    if header_guard:
      result.append(Generator.GUARD_END.format(header_guard))

    result.append('')

    return '\n'.join(result)
