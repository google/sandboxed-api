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

project(libidn2-sapi CXX C)
include(CTest)
include(GoogleTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   # Omit this to have the full Sandboxed API in IDE
                   EXCLUDE_FROM_ALL)
endif()

# Use pkg-config for the libidn2 include paths
find_package(PkgConfig REQUIRED)
pkg_check_modules(libidn2 REQUIRED IMPORTED_TARGET libidn2)

# System libidn2 needs to have the libidn2-dev and libunistring-dev packages
# installed. We cannot use pkg-config here, as libidn2's pkg-config file does
# not support static linking well.
find_library(libidn2 NAMES libidn2.a idn2)
find_library(libunistring NAMES libunistring.a unistring)

# Combine libidn2 and its dependency into a single static library
add_library(idn2_static STATIC
  "${SAPI_BINARY_DIR}/sapi_force_cxx_linkage.cc"
)
target_link_libraries(idn2_static PUBLIC
  "-Wl,--whole-archive,${libidn2},--no-whole-archive"
  "-Wl,--whole-archive,${libunistring},--no-whole-archive"
)

add_sapi_library(libidn2_sapi
  FUNCTIONS idn2_lookup_u8 idn2_register_u8
            idn2_strerror idn2_strerror_name
            idn2_free idn2_to_ascii_8z
            idn2_to_unicode_8z8z
  INPUTS "${libidn2_INCLUDEDIR}/idn2.h"
  LIBRARY idn2_static
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
add_library(sapi_contrib::libidn2 ALIAS libidn2_sapi_wrapper)
target_link_libraries(libidn2_sapi_wrapper
  # PUBLIC so that the include directories are included in the interface
  PUBLIC libidn2_sapi
         sapi::base
  PRIVATE absl::die_if_null
          idn2
)

if(BUILD_TESTING AND SAPI_BUILD_TESTING)
  add_subdirectory(tests)
endif()
