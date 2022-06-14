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
project(sapi_libxls C CXX)
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

FetchContent_Declare(libxls
  GIT_REPOSITORY https://github.com/libxls/libxls.git
  GIT_TAG        448240067919707eb95fb009f76f3fdb439b1427 # 2021-01-04
)

FetchContent_GetProperties(libxls)
if(NOT libxls_POPULATED)
  FetchContent_Populate(libxls)
  set(libxls_STATUS_FILE "${libxls_SOURCE_DIR}/config.status")
  if(EXISTS "${libxls_STATUS_FILE}")
    file(SHA256 "${libxls_STATUS_FILE}" _sapi_CONFIG_STATUS)
  endif()
  if(NOT _sapi_CONFIG_STATUS STREQUAL "${libxls_CONFIG_STATUS}")
    message("-- Configuring libxls...")
    execute_process(
      COMMAND autoreconf -i
      WORKING_DIRECTORY "${libxls_SOURCE_DIR}"
      RESULT_VARIABLE _sapi_libxls_autoreconf_result
    )
    if(NOT _sapi_libxls_autoreconf_result EQUAL "0")
      message(FATAL_ERROR "Configuration for libxls failed: "
                          "${_sapi_libxls_autoreconf_result}")
    endif()
    execute_process(
      COMMAND ./configure --disable-dependency-tracking
                          --disable-shared
                          --quiet
      WORKING_DIRECTORY "${libxls_SOURCE_DIR}"
      RESULT_VARIABLE _sapi_libxls_config_result
    )
    if(NOT _sapi_libxls_config_result EQUAL "0")
      message(FATAL_ERROR "Configuration for libxls failed: "
                          "${_sapi_libxls_config_result}")
    endif()
    file(SHA256 "${libxls_SOURCE_DIR}/config.status" _sapi_CONFIG_STATUS)
    set(libxls_CONFIG_STATUS "${_sapi_CONFIG_STATUS}" CACHE INTERNAL "")
  endif()
endif()

add_library(libxls STATIC
  "${libxls_SOURCE_DIR}/src/endian.c"
  "${libxls_SOURCE_DIR}/src/locale.c"
  "${libxls_SOURCE_DIR}/src/ole.c"
  "${libxls_SOURCE_DIR}/src/xls.c"
  "${libxls_SOURCE_DIR}/src/xlstool.c"
)
target_include_directories(libxls PUBLIC
  "${libxls_SOURCE_DIR}"
  "${libxls_SOURCE_DIR}/include"
)

configure_file(xls.gen.h.in xls.gen.h)

add_sapi_library(sapi_libxls
  FUNCTIONS xls_open_file

            xls_getWorkSheet
            xls_parseWorkSheet

            xls_cell

            xls_close_WS
            xls_close_WB

            xls_getError
  INPUTS "${PROJECT_BINARY_DIR}/xls.gen.h"
  LIBRARY libxls
  LIBRARY_NAME Libxls
  NAMESPACE ""
)
add_library(sapi_contrib::libxls ALIAS sapi_libxls)
target_include_directories(sapi_libxls INTERFACE
  "${PROJECT_BINARY_DIR}"
)

if(SAPI_BUILD_EXAMPLES)
  add_subdirectory(example)
endif()

if(BUILD_TESTING AND SAPI_BUILD_TESTING)
  add_subdirectory(test)
endif()
