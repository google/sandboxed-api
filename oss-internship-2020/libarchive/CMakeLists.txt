# Copyright 2020 Google LLC
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

cmake_minimum_required(VERSION 3.16)

project(libarchive_sapi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED 17)
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

# Build SAPI library
set(SAPI_ROOT "" CACHE PATH "Path to the Sandboxed API source tree")

include(FetchContent)
FetchContent_Declare(
  libarchive
  GIT_REPOSITORY https://github.com/libarchive/libarchive
  PATCH_COMMAND cd libarchive && patch < ${CMAKE_SOURCE_DIR}/patches/header.patch && patch < ${CMAKE_SOURCE_DIR}/patches/archive_virtual.patch
)
FetchContent_MakeAvailable(libarchive)

add_subdirectory("${SAPI_ROOT}"
  "${CMAKE_BINARY_DIR}/sandboxed-api-build"
  EXCLUDE_FROM_ALL
)

file(STRINGS functions_to_sandbox.txt FUNCTIONS_LIST)

add_sapi_library(
  libarchive_sapi

  FUNCTIONS ${FUNCTIONS_LIST}

  INPUTS
  ${CMAKE_BINARY_DIR}/_deps/libarchive-src/libarchive/archive.h
  ${CMAKE_BINARY_DIR}/_deps/libarchive-src/libarchive/archive_entry.h

  LIBRARY archive_static
  LIBRARY_NAME Libarchive
  NAMESPACE ""
)

target_include_directories(libarchive_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"  # To find the generated SAPI header
)

add_subdirectory(examples)
add_subdirectory(test)
add_subdirectory(ld_preload_example)
