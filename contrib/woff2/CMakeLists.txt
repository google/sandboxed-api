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

project(woff2-sapi CXX C)
include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   # Omit this to have the full Sandboxed API in IDE
                   EXCLUDE_FROM_ALL)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(WOFF2_ENC REQUIRED IMPORTED_TARGET GLOBAL libwoff2enc)
pkg_check_modules(WOFF2_DEC REQUIRED IMPORTED_TARGET GLOBAL libwoff2dec)
pkg_check_modules(WOFF2_COMMON REQUIRED IMPORTED_TARGET GLOBAL libwoff2common)

add_library(woff2_sapi_wrapper woff2_wrapper.cc woff2_wrapper.h)
target_link_libraries(woff2_sapi_wrapper
  PRIVATE
    PkgConfig::WOFF2_ENC
    PkgConfig::WOFF2_DEC
    PkgConfig::WOFF2_COMMON
)

add_sapi_library(woff2_sapi
  FUNCTIONS
    WOFF2_ConvertWOFF2ToTTF
    WOFF2_ConvertTTFToWOFF2
    WOFF2_Free
  INPUTS
    "woff2_wrapper.h"
  LIBRARY
    woff2_sapi_wrapper
  LIBRARY_NAME
    WOFF2
  NAMESPACE
    sapi_woff2
)
add_library(sapi_contrib::woff2 ALIAS woff2_sapi)
target_include_directories(woff2_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"
  "${SAPI_SOURCE_DIR}"
)

if(BUILD_TESTING AND SAPI_BUILD_TESTING)
  enable_testing()
  add_executable(woff2_sapi_test
    woff2_sapi_test.cc
  )
  target_link_libraries(woff2_sapi_test PRIVATE
    absl::flags
    absl::flags_parse
    sapi_contrib::woff2
    sapi::test_main
  )
  gtest_discover_tests(woff2_sapi_test PROPERTIES
    ENVIRONMENT "TEST_DATA_DIR=${PROJECT_SOURCE_DIR}/testdata")
endif()
