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

if(POLICY "CMP0115")
  cmake_policy(SET "CMP0115" NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(SAPI_ROOT "" CACHE PATH "Path to the Sandboxed API source tree")

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   EXCLUDE_FROM_ALL)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(S_FILE REQUIRED IMPORTED_TARGET libmagic)

add_sapi_library(file_sapi
  FUNCTIONS
    magic_open
    magic_close
    magic_getpath
    magic_file
    magic_descriptor
    magic_buffer
    magic_error
    magic_getflags
    magic_setflags
    magic_version
    magic_load
    magic_load_buffers
    magic_compile
    magic_check
    magic_list
    magic_errno
    magic_getparam
    magic_setparam
  INPUTS "${S_FILE_INCLUDEDIR}/magic.h"
  LIBRARY "${S_FILE_LIBRARIES}"
  LIBRARY_NAME "File"
  NAMESPACE "file_sapi"
)
target_include_directories(file_sapi INTERFACE "${PROJECT_BINARY_DIR}")

include(GoogleTest)

add_executable(file_sapi_test file_sapi_test.cc)

target_link_libraries(file_sapi_test PRIVATE
  file_sapi
  sapi::base
  gtest
  gmock
)

gtest_discover_tests(file_sapi_test PROPERTIES ENVIRONMENT "TEST_FILES_DIR=${PROJECT_SOURCE_DIR}/testdata")
