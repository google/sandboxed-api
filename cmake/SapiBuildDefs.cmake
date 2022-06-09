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

# Embeds arbitrary binary data into a static library.
#
# NAME specifies the name for this target.
# NAMESPACE is the C++ namespace the generated code is placed in. Can be empty.
# SOURCES is a list of files that should be embedded. If a source names a
#   target the target binary is embedded instead.
macro(sapi_cc_embed_data)
  cmake_parse_arguments(_sapi_embed "" "OUTPUT_NAME;NAME;NAMESPACE" "SOURCES"
                        ${ARGN})
  foreach(src IN LISTS _sapi_embed_SOURCES)
    if(TARGET "${src}")
      get_target_property(_sapi_embed_src_OUTPUT_NAME ${src} OUTPUT_NAME)
      if(NOT _sapi_embed_src_OUTPUT_NAME)
        set(_sapi_embed_src_OUTPUT_NAME "${src}")
      endif()
      list(APPEND _sapi_embed_in
          "${CMAKE_CURRENT_BINARY_DIR}/${_sapi_embed_src_OUTPUT_NAME}")
    else()
      list(APPEND _sapi_embed_in "${src}")
    endif()
  endforeach()
  file(RELATIVE_PATH _sapi_embed_pkg
                     "${PROJECT_BINARY_DIR}"
                     "${CMAKE_CURRENT_BINARY_DIR}")
  if(NOT _sapi_embed_OUTPUT_NAME)
    set(_sapi_embed_OUTPUT_NAME "${_sapi_embed_NAME}")
  endif()
  add_custom_command(
    OUTPUT "${_sapi_embed_OUTPUT_NAME}.h"
           "${_sapi_embed_OUTPUT_NAME}.cc"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMAND filewrapper "${_sapi_embed_pkg}"
                        "${_sapi_embed_OUTPUT_NAME}"
                        "${_sapi_embed_NAMESPACE}"
                        "${CMAKE_CURRENT_BINARY_DIR}/${_sapi_embed_OUTPUT_NAME}.h"
                        "${CMAKE_CURRENT_BINARY_DIR}/${_sapi_embed_OUTPUT_NAME}.cc"
                        ${_sapi_embed_in}
    DEPENDS ${_sapi_embed_SOURCES}
    VERBATIM
  )
  add_library("${_sapi_embed_NAME}" STATIC
    "${_sapi_embed_OUTPUT_NAME}.h"
    "${_sapi_embed_OUTPUT_NAME}.cc"
  )
  target_link_libraries("${_sapi_embed_NAME}" PRIVATE
    sapi::base
    absl::core_headers
  )
endmacro()

# Adds a library target implementing a sandboxed API for another library.
# The first argument is the target name, similar to the native add_library().
# This function implements the same functionality as the Bazel version in
# sandboxed_api/bazel/sapi.bzl.
#
# SOURCES Any additional sources to include with the Sandboxed API library.
#   Typically not necessary, unless the sandbox definition should be in a .cc
#   file instead of the customary "sandbox.h" header. Bazel also has a "hdrs"
#   attribute, but CMake does not distinguish headers from sources.
# FUNCTIONS A list of functions that to use in from host code. Leaving this
#   list empty will export and wrap all functions found in the library.
# NOEMBED Whether the SAPI library should be embedded inside host code, so the
#   SAPI Sandbox can be initialized with the
#   ::sapi::Sandbox::Sandbox(FileToc*) constructor.
# LIBRARY The library target to sandbox and expose to the host code (required).
# LIBRARY_NAME The name of the class which will proxy the library functions
#   from the functions list (required). You will call functions from the
#   sandboxed library via instances of this class.
# INPUTS List of source files which the SAPI interface generator should scan
#   for function declarations. Library header files are always scanned, so
#   this can usually be empty/omitted.
# NAMESPACE C++ namespace identifier to place API class defined by
#   LIBRARY_NAME into.
# HEADER If set, does not generate an interface header, but uses the one
#   specified.
# API_VERSION Which version of the Sandboxed API to generate. Currently, only
#   version "1" is defined.
function(add_sapi_library)
  set(_sapi_opts NOEMBED)
  set(_sapi_one_value HEADER LIBRARY LIBRARY_NAME NAMESPACE API_VERSION)
  set(_sapi_multi_value SOURCES FUNCTIONS INPUTS)
  cmake_parse_arguments(PARSE_ARGV 0 _sapi "${_sapi_opts}"
                        "${_sapi_one_value}" "${_sapi_multi_value}")
  set(_sapi_NAME "${ARGV0}")

  if(_sapi_API_VERSION AND NOT _sapi_API_VERSION VERSION_EQUAL "1")
    message(FATAL_ERROR "API_VERSION \"1\" is the only one defined right now")
  endif()

  set(_sapi_gen_header "${_sapi_NAME}.sapi.h")
  foreach(func IN LISTS _sapi_FUNCTIONS)
    list(APPEND _sapi_exported_funcs "LINKER:--export-dynamic-symbol,${func}")
  endforeach()
  if(NOT _sapi_exported_funcs)
    set(_sapi_exported_funcs LINKER:--allow-multiple-definition)
  endif()

  # The sandboxed binary
  set(_sapi_bin "${_sapi_NAME}.bin")
  add_executable("${_sapi_bin}"
    "${SAPI_BINARY_DIR}/sapi_force_cxx_linkage.cc"
  )
  target_link_libraries("${_sapi_bin}" PRIVATE
    -fuse-ld=gold
    -Wl,--whole-archive "${_sapi_LIBRARY}" -Wl,--no-whole-archive
    sapi::client
    ${CMAKE_DL_LIBS}
  )
  target_link_options("${_sapi_bin}" PRIVATE
    LINKER:-E
    ${_sapi_exported_funcs}
  )

  if(NOT _sapi_NOEMBED)
    set(_sapi_embed "${_sapi_NAME}_embed")
    sapi_cc_embed_data(NAME "${_sapi_embed}"
      NAMESPACE "${_sapi_NAMESPACE}"
      SOURCES "${_sapi_bin}"
    )
  endif()

  # Interface
  list(JOIN _sapi_FUNCTIONS "," _sapi_funcs)
  foreach(src IN LISTS _sapi_INPUTS)
    get_filename_component(src "${src}" ABSOLUTE)
    list(APPEND _sapi_full_inputs "${src}")
  endforeach()
  if(NOT _sapi_NOEMBED)
    set(_sapi_embed_dir "${CMAKE_CURRENT_BINARY_DIR}")
    set(_sapi_embed_name "${_sapi_NAME}")
  endif()

  list(APPEND _sapi_generator_args
    "--sapi_name=${_sapi_LIBRARY_NAME}"
    "--sapi_out=${_sapi_gen_header}"
    "--sapi_embed_dir=${_sapi_embed_dir}"
    "--sapi_embed_name=${_sapi_embed_name}"
    "--sapi_functions=${_sapi_funcs}"
    "--sapi_ns=${_sapi_NAMESPACE}"
  )
  if(SAPI_ENABLE_GENERATOR)
    list(APPEND _sapi_generator_command
      sapi_generator_tool
      -p "${CMAKE_CURRENT_BINARY_DIR}"
      ${_sapi_generator_args}
      ${_sapi_full_inputs}
    )
    add_custom_command(
      OUTPUT "${_sapi_gen_header}"
      COMMAND ${_sapi_generator_command}
      COMMENT "Generating interface"
      DEPENDS ${_sapi_INPUTS}
      VERBATIM
    )
  else()
    set(_sapi_isystem "${_sapi_NAME}.isystem")
    list(JOIN _sapi_full_inputs "," _sapi_full_inputs)
    list(APPEND _sapi_generator_command
      "${SAPI_PYTHON3_EXECUTABLE}" -B
      "${SAPI_SOURCE_DIR}/sandboxed_api/tools/generator2/sapi_generator.py"
      ${_sapi_generator_args}
      "--sapi_isystem=${_sapi_isystem}"
      "--sapi_in=${_sapi_full_inputs}"
    )
    add_custom_command(
      OUTPUT "${_sapi_gen_header}" "${_sapi_isystem}"
      COMMAND sh -c
              "${CMAKE_CXX_COMPILER} -E -x c++ -v /dev/null 2>&1 | \
               awk '/> search starts here:/{f=1;next}/^End of search/{f=0}f{print $1}' \
               > \"${_sapi_isystem}\""
      COMMAND ${_sapi_generator_command}
      COMMENT "Generating interface"
      DEPENDS ${_sapi_INPUTS}
      VERBATIM
    )
  endif()

  # Library with the interface
  if(NOT _sapi_SOURCES)
    list(APPEND _sapi_SOURCES
      "${SAPI_BINARY_DIR}/sapi_force_cxx_linkage.cc"
    )
  endif()
  add_library("${_sapi_NAME}" STATIC
    "${_sapi_gen_header}"
    ${_sapi_SOURCES}
  )
  target_link_libraries("${_sapi_NAME}" PUBLIC
    absl::status
    absl::statusor
    sapi::sapi
    sapi::status
    sapi::vars
  )
  if(NOT _sapi_NOEMBED)
    target_link_libraries("${_sapi_NAME}" PUBLIC
      "${_sapi_embed}"
    )
  endif()
endfunction()

# Wrapper for gtest_discover_tests to exclude tests discover when cross compiling.
macro(gtest_discover_tests_xcompile)
  if (NOT CMAKE_CROSSCOMPILING)
    gtest_discover_tests(${ARGV})
  endif()
endmacro()
