# Copyright 2022 Google LLC
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

cmake_minimum_required(VERSION 3.13..3.22)

project(sapi_hunspell CXX)
include(CTest)
include(GoogleTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   EXCLUDE_FROM_ALL)
endif()

FetchContent_Declare(libhunspell
  GIT_REPOSITORY https://github.com/hunspell/hunspell.git
  GIT_TAG        31e6d6323026a3bef12c5912ce032d88bfef2091 # 2021-10-15
)

FetchContent_GetProperties(libhunspell)
if(NOT libhunspell_POPULATED)
  FetchContent_Populate(libhunspell)
  set(libhunspell_STATUS_FILE "${libhunspell_SOURCE_DIR}/config.status")
  if(EXISTS "${libhunspell_STATUS_FILE}")
    file(SHA256 "${libhunspell_STATUS_FILE}" _sapi_CONFIG_STATUS)
  endif()
  if(NOT _sapi_CONFIG_STATUS STREQUAL "${libhunspell_CONFIG_STATUS}")
    message("-- Configuring libhunspell...")
    execute_process(
      COMMAND autoreconf -i
      WORKING_DIRECTORY "${libhunspell_SOURCE_DIR}"
      RESULT_VARIABLE _sapi_libhunspell_autoreconf_result
    )
    if(NOT _sapi_libhunspell_autoreconf_result EQUAL "0")
      message(FATAL_ERROR "Configuration for libhunspell failed: "
                          "${_sapi_libhunspell_autoreconf_result}")
    endif()
    execute_process(
      COMMAND ./configure --disable-dependency-tracking
                          --quiet
      WORKING_DIRECTORY "${libhunspell_SOURCE_DIR}"
      RESULT_VARIABLE _sapi_libhunspell_config_result
    )
    if(NOT _sapi_libhunspell_config_result EQUAL "0")
      message(FATAL_ERROR "Configuration for libhunspell failed: "
                          "${_sapi_libhunspell_config_result}")
    endif()
    file(SHA256 "${libhunspell_SOURCE_DIR}/config.status" _sapi_CONFIG_STATUS)
    set(libhunspell_CONFIG_STATUS "${_sapi_CONFIG_STATUS}" CACHE INTERNAL "")
  endif()
endif()

add_library(hunspell STATIC
  "${libhunspell_SOURCE_DIR}/src/hunspell/affentry.cxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/affentry.hxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/affixmgr.cxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/affixmgr.hxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/atypes.hxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/baseaffix.hxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/csutil.cxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/csutil.hxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/filemgr.cxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/filemgr.hxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/hashmgr.cxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/hashmgr.hxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/htypes.hxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/hunspell.cxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/hunspell.h"
  "${libhunspell_SOURCE_DIR}/src/hunspell/hunspell.hxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/hunzip.cxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/hunzip.hxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/langnum.hxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/phonet.cxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/phonet.hxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/replist.cxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/replist.hxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/suggestmgr.cxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/suggestmgr.hxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/utf_info.hxx"
  "${libhunspell_SOURCE_DIR}/src/hunspell/w_char.hxx"
)

target_include_directories(hunspell PUBLIC
  "${libhunspell_SOURCE_DIR}/src/hunspell"
)

set(libhunspell_INCLUDE_DIR "${libhunspell_SOURCE_DIR}/src/hunspell")

add_sapi_library(sapi_hunspell
  FUNCTIONS Hunspell_create
            Hunspell_create_key
            Hunspell_destroy

            Hunspell_spell
            Hunspell_get_dic_encoding

            Hunspell_suggest
            Hunspell_analyze

            Hunspell_add
            Hunspell_remove

            Hunspell_free_list

  INPUTS "${libhunspell_INCLUDE_DIR}/hunspell.h"

  LIBRARY hunspell
  LIBRARY_NAME Hunspell
  NAMESPACE ""
)
add_library(sapi_contrib::hunspell ALIAS sapi_hunspell)
target_include_directories(sapi_hunspell INTERFACE
  "${PROJECT_BINARY_DIR}"
)

if(SAPI_BUILD_EXAMPLES)
  add_subdirectory(example)
endif()

if(BUILD_TESTING AND SAPI_BUILD_TESTING)
  add_subdirectory(test)
endif()
