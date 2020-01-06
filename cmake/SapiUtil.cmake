# Copyright 2020 Google LLC. All Rights Reserved.
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

# Creates an alias for SOURCE, called DESTINATION.
#
# On platforms that support them, this rule will effectively create a symlink.
#
# SOURCE may be relative to CMAKE_CURRENT_SOURCE_DIR, or absolute.
# DESTINATION may relative to CMAKE_CURRENT_BINARY_DIR, or absolute.
#
# Adapted from https://github.com/google/binexport/blob/master/util.cmake
function(create_directory_symlink SOURCE DESTINATION)
  get_filename_component(_destination_parent "${DESTINATION}" DIRECTORY)
  file(MAKE_DIRECTORY "${_destination_parent}")

  if(WIN32)
    file(TO_NATIVE_PATH "${SOURCE}" _native_source)
    file(TO_NATIVE_PATH "${DESTINATION}" _native_destination)
    execute_process(COMMAND $ENV{ComSpec} /c
        mklink /J "${_native_destination}" "${_native_source}" ERROR_QUIET)
  else()
    execute_process(COMMAND ${CMAKE_COMMAND} -E
      create_symlink "${SOURCE}" "${DESTINATION}")
  endif()
endfunction()

# Implements list(JOIN ...) for CMake < 3.12.
function(list_join LIST SEP OUTPUT)
  foreach(item IN LISTS ${LIST})
    set(_concat "${_concat}${SEP}${item}")
  endforeach()
  string(LENGTH "${SEP}" _len)
  string(SUBSTRING "${_concat}" ${_len} -1 _concat)
  set(${OUTPUT} "${_concat}" PARENT_SCOPE)
endfunction()

# Helper function that behaves just like Protobuf's protobuf_generate_cpp(),
# except that it strips import paths. This is necessary, because CMake's
# protobuf rules don't work well with imports across different directories.
function(protobuf_generate_cpp SRCS HDRS)
  cmake_parse_arguments(_pb "" "EXPORT_MACRO" "" ${ARGN})
  if(NOT _pb_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "protobuf_generate_cpp() missing proto files")
    return()
  endif()

  foreach(_file IN LISTS _pb_UNPARSED_ARGUMENTS)
    file(READ "${_file}" _pb_orig)
    string(REGEX REPLACE "import \".*/([^/]+\\.proto)\""
                         "import \"\\1\"" _pb_repl "${_pb_orig}")
    set(_file "${CMAKE_CURRENT_BINARY_DIR}/${_file}")
    file(WRITE "${_file}" "${_pb_repl}")
    list(APPEND _pb_files "${_file}")
  endforeach()

  set(_outvar)
  protobuf_generate(APPEND_PATH
                    LANGUAGE cpp
                    EXPORT_MACRO ${_pb_EXPORT_MACRO}
                    OUT_VAR _outvar
                    PROTOS ${_pb_files})
  set(${SRCS})
  set(${HDRS})
  foreach(_file IN LISTS _outvar)
    if(_file MATCHES "cc$")
      list(APPEND ${SRCS} ${_file})
    else()
      list(APPEND ${HDRS} ${_file})
    endif()
  endforeach()
  set(${SRCS} ${${SRCS}} PARENT_SCOPE)
  set(${HDRS} ${${HDRS}} PARENT_SCOPE)
endfunction()

if(NOT COMMAND protobuf_generate)
  # Runs the protocol buffer compiler on the given proto files. Compatible
  # with the upstream version and included here so we can add_subdirectory()
  # the protobuf source tree.
  function(protobuf_generate)
    set(_options APPEND_PATH)
    set(_singleargs LANGUAGE OUT_VAR EXPORT_MACRO PROTOC_OUT_DIR TARGET)
    set(_multiargs PROTOS IMPORT_DIRS GENERATE_EXTENSIONS)
    cmake_parse_arguments(_pb "${_options}" "${_singleargs}" "${_multiargs}"
                              "${ARGN}")

    if(NOT _pb_PROTOS AND NOT _pb_TARGET)
      message(FATAL_ERROR "protobuf_generate missing targets or sources")
      return()
    endif()

    if(NOT _pb_OUT_VAR AND NOT _pb_TARGET)
      message(FATAL_ERROR "protobuf_generate missing target or output var")
      return()
    endif()

    if(NOT _pb_LANGUAGE)
      set(_pb_LANGUAGE cpp)
    else()
      string(TOLOWER ${_pb_LANGUAGE} _pb_LANGUAGE)
    endif()

    if(NOT _pb_PROTOC_OUT_DIR)
      set(_pb_PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
    endif()

    if(_pb_EXPORT_MACRO AND _pb_LANGUAGE STREQUAL cpp)
      set(_dll_export_decl "dllexport_decl=${_pb_EXPORT_MACRO}:")
    endif()

    if(NOT _pb_GENERATE_EXTENSIONS)
      if(_pb_LANGUAGE STREQUAL cpp)
        set(_pb_GENERATE_EXTENSIONS .pb.h .pb.cc)
      elseif(_pb_LANGUAGE STREQUAL python)
        set(_pb_GENERATE_EXTENSIONS _pb2.py)
      else()
        message(FATAL_ERROR
            "protobuf_generate given unknown language ${_pb_LANGUAGE}")
        return()
      endif()
    endif()

    if(_pb_TARGET)
      get_target_property(_source_list ${_pb_TARGET} SOURCES)
      foreach(_file IN LISTS _source_list)
        if(_file MATCHES "proto$")
          list(APPEND _pb_PROTOS "${_file}")
        endif()
      endforeach()
    endif()

    if(NOT _pb_PROTOS)
      message(FATAL_ERROR "protobuf_generate could not find any .proto files")
      return()
    endif()

    # Create an include path for each file specified
    foreach(_file ${_pb_PROTOS})
      get_filename_component(_abs_file ${_file} ABSOLUTE)
      get_filename_component(_abs_path ${_abs_file} PATH)
      list(FIND _protobuf_include_path "${_abs_path}" _contains_already)
      if(${_contains_already} EQUAL -1)
        list(APPEND _protobuf_include_path -I ${_abs_path})
      endif()
    endforeach()

    foreach(_dir IN LISTS _pb_IMPORT_DIRS)
      get_filename_component(_abs_path "${_dir}" ABSOLUTE)
      list(FIND _protobuf_include_path "${_abs_path}" _contains_already)
      if(${_contains_already} EQUAL -1)
        list(APPEND _protobuf_include_path -I "${_abs_path}")
      endif()
    endforeach()

    set(_generated_srcs_all)
    foreach(_proto IN LISTS _pb_PROTOS)
      get_filename_component(_abs_file ${_proto} ABSOLUTE)
      get_filename_component(_abs_dir ${_abs_file} DIRECTORY)
      get_filename_component(_basename ${_proto} NAME_WE)
      file(RELATIVE_PATH _rel_dir "${CMAKE_CURRENT_SOURCE_DIR}" "${_abs_dir}")

      set(_generated_srcs)
      foreach(_ext ${_pb_GENERATE_EXTENSIONS})
        list(APPEND _generated_srcs
                    "${_pb_PROTOC_OUT_DIR}/${_rel_dir}/${_basename}${_ext}")
      endforeach()
      list(APPEND _generated_srcs_all ${_generated_srcs})

      add_custom_command(OUTPUT ${_generated_srcs}
                         COMMAND  protobuf::protoc
                         ARGS --${_pb_LANGUAGE}_out
                              ${_dll_export_decl}${_pb_PROTOC_OUT_DIR}
                              ${_protobuf_include_path}
                              ${_abs_file}
                         DEPENDS ${_abs_file} protobuf::protoc
                         COMMENT "Running ${_pb_LANGUAGE} protoc on ${_proto}"
                         VERBATIM)
    endforeach()

    set_source_files_properties(${_generated_srcs_all}
                                PROPERTIES GENERATED TRUE)
    if(_pb_OUT_VAR)
      set(${_pb_OUT_VAR} ${_generated_srcs_all} PARENT_SCOPE)
    endif()
    if(_pb_TARGET)
      target_sources(${_pb_TARGET} PRIVATE ${_generated_srcs_all})
    endif()
  endfunction()
endif()
