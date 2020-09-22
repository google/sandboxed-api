# Copyright 2020 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Script generating a wrapper API for LibUV."""

import sys
import re
import os


def get_var_type(string):
    """Gets the type from an argument variable
    (e.g. "int x" -> "int")
    """

    var = string.strip()

    # Unnamed variable
    if var in ("void", "...") or var[-1] == "*":
        return var

    return " ".join(var.split(" ")[:-1]).strip()


def get_var_name(string):
    """Gets the name from an argument variable
    (e.g. "int x" -> "x")
    """

    var = string.strip()

    # Not an actual variable
    if var in ("void", "..."):
        return ""

    # Unnamed variable, use an arbitrary name
    if var[-1] == "*":
        return var.split("_")[1]

    return var.split(" ")[-1].strip()


def fix_method_type(string):
    """Fixes the method type
    (e.g. "const int*" -> "const void*")
    """

    method_type = string.strip()

    # Const pointer
    if "*" in method_type and "const" in method_type:
        return "const void*"

    # Regular pointer
    if "*" in method_type:
        return "void*"

    # Not a pointer
    return method_type


def fix_argument(string):
    """Fixes an argument
    (e.g. "const int* x" -> "const void* x")
    """

    arg_type = get_var_type(string)
    arg_name = get_var_name(string)

    # Array argument, becomes a pointer
    if "[" in arg_name:
        arg_type += "*"
        arg_name = arg_name.split("[")[0] + arg_name.split("]")[-1]

    # Pointer (in LibUV, types endind in "_cb" or "_func" are pointers)
    if "*" in arg_type or "_cb" in arg_type or "_func" in arg_type:
        if "const" in arg_type:
            return "const void* " + arg_name
        return "void* " + arg_name

    # Not a pointer
    return arg_type + " " + arg_name


def fix_call_argument(string):
    """Fixes an argument in a call the orignal method
    (e.g. "const int* x" -> "reinterpret_cast<const int*>(x)")
    """

    arg_type = get_var_type(string)
    arg_name = get_var_name(string)

    # Array argument, becomes a pointer
    if "[" in arg_name:
        arg_type += "*"
        arg_name = arg_name.split("[")[0] + arg_name.split("]")[-1]

    # Pointer (in LibUV, types endind in "_cb" or "_func" are pointers)
    if "*" in arg_type or "_cb" in arg_type or "_func" in arg_type:
        return "reinterpret_cast<" + arg_type + ">(" + arg_name + ")"

    # Not a pointer
    return arg_name


def read_file(filename):
    """Returns contents of filename as a string"""

    file = open(filename, "r")
    return str(file.read())


def clean_file(text):
    """Prepares the file for parsing
    In particular, removes comments and macros from text
    Additionally, moves pointer asterisks next to its type
    """

    result = text
    result = re.sub(r"//.*?\n", "", result, flags=re.S)
    result = re.sub(r"/\*.*?\*/", "", result, flags=re.S)
    result = re.sub(r"#.*?\n", "", result, flags=re.S)
    result = result.replace(" *", "* ")
    return result


def get_signatures(text):
    """Gets the signatures of all the methods in the header
    .. note:: This method only works on a certain version of LibUV's header
    """

    signatures = [x.split(";")[0].strip() for x in text.split("UV_EXTERN")[1:]]
    method_types = [" ".join(s.split("(")[0].split(" ")[:-1]).strip()
                    for s in signatures]
    names = [s.split("(")[0].split(" ")[-1].strip() for s in signatures]
    arguments = [s.split("(")[1][:-1] for s in signatures]
    arguments_lists = [[x.strip() for x in a.split(",")] for a in arguments]
    return zip(method_types, names, arguments_lists)


def write_method(method_type, name, arguments_list, header, source):
    """Writes the method to both the header and the source files"""

    header.write(fix_method_type(method_type) + " sapi_" + name + "(" +
                 ", ".join(map(fix_argument, arguments_list)) + ");\n\n")
    source.write(fix_method_type(method_type) + " sapi_" + name + "(" +
                 ", ".join(map(fix_argument, arguments_list)) + ") {\n")
    source.write("  return " + name + "(" +
                 ", ".join(map(fix_call_argument, arguments_list)) + ");\n")
    source.write("}\n\n")


def write_text(text, file):
    """Writes text to file
    Useful for additional methods, includes, extern "C"...
    """

    file.write(text)


def generate_wrapper():
    """Generates the wrapper"""

    text = read_file(sys.argv[1])
    text = clean_file(text)
    signatures = get_signatures(text)

    header = open(sys.argv[2], "w")
    source = open(sys.argv[3], "w")

    write_text("#include <uv.h>\n\n", header)
    write_text("extern \"C\" {\n\n", header)
    write_text("#include \"" + os.path.abspath(header.name) + "\"\n\n", source)

    for (method_type, name, arguments_list) in signatures:
        # These wrapper methods are manually added at the end
        if name in ("uv_once", "uv_loop_configure"):
            continue
        write_method(method_type, name, arguments_list, header, source)

    # Add sapi_uv_once (uv_once uses a differnet kind of callback)
    write_text("void sapi_uv_once(void* guard, void (*callback)(void));\n\n",
               header)
    write_text("void sapi_uv_once(void* guard, void (*callback)(void)) {\n" +
               "  return uv_once(reinterpret_cast<uv_once_t*>(guard)," +
               "callback);\n" + "}\n\n", source)

    # Add sapi_uv_loop_configure (uv_loop_configure is variadic)
    write_text("int sapi_uv_loop_configure(void* loop, uv_loop_option option)" +
               ";\n\n", header)
    write_text("int sapi_uv_loop_configure(void* loop, uv_loop_option option)" +
               " {\n  return uv_loop_configure(" +
               "reinterpret_cast<uv_loop_t*>(loop), option);\n" + "}\n\n",
               source)

    # Add sapi_uv_loop_configure_int (uv_loop_configure is variadic)
    write_text("int sapi_uv_loop_configure_int(void* loop, " +
               "uv_loop_option option, int ap);\n\n", header)
    write_text("int sapi_uv_loop_configure_int(void* loop, " +
               "uv_loop_option option, int ap) {\n" +
               "  return uv_loop_configure(" +
               "reinterpret_cast<uv_loop_t*>(loop), option, ap);\n}\n\n",
               source)

    write_text("}  // extern \"C\"\n", header)


generate_wrapper()
