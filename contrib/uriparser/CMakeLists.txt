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

project(sapi_uriparser CXX)
include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   EXCLUDE_FROM_ALL)
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(URIPARSER_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(URIPARSER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(URIPARSER_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(uriparser
  GIT_REPOSITORY https://github.com/uriparser/uriparser
  GIT_TAG        a259209a57f7123d4bc422336ce0d420d41f4f5e
)
FetchContent_MakeAvailable(uriparser)

configure_file(uri.gen.h.in uri.gen.h)

add_sapi_library(
  sapi_uriparser

  FUNCTIONS
    uriParseUriA
    uriEscapeA

    uriAddBaseUriA
    uriRemoveBaseUriA

    uriToStringA
    uriToStringCharsRequiredA

    uriNormalizeSyntaxMaskRequiredA
    uriNormalizeSyntaxExA

    uriDissectQueryMallocA
    uriFreeQueryListA

    uriFreeUriMembersA

  INPUTS
    "${CMAKE_CURRENT_BINARY_DIR}/uri.gen.h"

  LIBRARY uriparser
  LIBRARY_NAME Uriparser
  NAMESPACE ""
)
add_library(sapi_contrib::uriparser ALIAS sapi_uriparser)
target_include_directories(sapi_uriparser INTERFACE
  "${PROJECT_BINARY_DIR}"
  "${SAPI_SOURCE_DIR}"
)

if(SAPI_BUILD_EXAMPLES)
  add_subdirectory(example)
endif()

if(BUILD_TESTING AND SAPI_BUILD_TESTING)
  add_subdirectory(test)
endif()
