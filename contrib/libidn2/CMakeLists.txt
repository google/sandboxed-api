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

cmake_minimum_required(VERSION 3.13)

project(test CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(SAPI_ROOT "${PROJECT_SOURCE_DIR}/../.." CACHE PATH "Path to the Sandboxed API source tree")
set(SAPI_ENABLE_EXAMPLES OFF CACHE BOOL "")
set(SAPI_ENABLE_TESTS OFF CACHE BOOL "")

add_subdirectory("${SAPI_ROOT}"
                 "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                 # Omit this to have the full Sandboxed API in IDE
                 EXCLUDE_FROM_ALL)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBIDN2 REQUIRED IMPORTED_TARGET libidn2)

add_sapi_library(libidn2_sapi
  FUNCTIONS idn2_lookup_u8 idn2_register_u8
            idn2_strerror idn2_strerror_name
            idn2_free idn2_to_ascii_8z
            idn2_to_unicode_8z8z
  INPUTS "${LIBIDN2_INCLUDEDIR}/idn2.h"
  LIBRARY idn2
  LIBRARY_NAME IDN2
  NAMESPACE ""
)

target_include_directories(libidn2_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"
)

add_library(libidn2_sapi_wrapper
  libidn2_sapi.cc
  libidn2_sapi.h
)

target_link_libraries(libidn2_sapi_wrapper
  # PUBLIC so that the include directories are included in the interface
  PUBLIC libidn2_sapi sapi::base
  PRIVATE idn2
)

if(SAPI_ENABLE_TESTS)
  add_subdirectory(tests)
endif()
