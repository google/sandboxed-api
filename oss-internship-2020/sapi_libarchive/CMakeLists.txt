# Copyright 2020 Google LLC
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

cmake_minimum_required(VERSION 3.16)

project(libarchive_sapi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED 17)

# Build SAPI library
set(SAPI_ROOT "/usr/local/google/home/amedar/internship/sandboxed-api" CACHE PATH "Path to the Sandboxed API source tree")

file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/patches")

#add_custom_command(
#    OUTPUT "${PROJECT_BINARY_DIR}/patches/archive.h"
#    COMMENT "Applying patches."
#    COMMAND cp "${PROJECT_SOURCE_DIR}/patches/header.patch" "${PROJECT_BINARY_DIR}/patches/"
#    COMMAND cp "${PROJECT_SOURCE_DIR}/libarchive/libarchive/archive.h" "${PROJECT_BINARY_DIR}/patches/"
#    COMMAND cd "${PROJECT_BINARY_DIR}/patches" && patch < header.patch
#    COMMAND cp "${PROJECT_BINARY_DIR}/patches/archive.h" "${PROJECT_SOURCE_DIR}/libarchive/libarchive/"
#)
#
#set_property(
#    SOURCE
#    "${PROJECT_SOURCE_DIR}/libarchive/libarchive/archive.h"
#    APPEND PROPERTY OBJECT_DEPENDS
#    "${PROJECT_BINARY_DIR}/patches/archive.h"
#)


add_subdirectory(libarchive)

#set_property(SOURCE
#    "${PROJECT_SOURCE_DIR}/libarchive/libarchive/archive.h"
#    APPEND PROPERTY OBJECT_DEPENDS
#    "${PROJECT_SOURCE_DIR}/patches/archive.h"
#)

add_subdirectory("${SAPI_ROOT}"
  "${CMAKE_BINARY_DIR}/sandboxed-api-build"
  EXCLUDE_FROM_ALL
)

file(STRINGS functions_to_sandbox.txt FUNCTIONS_LIST)

add_sapi_library(
  libarchive_sapi

  FUNCTIONS ${FUNCTIONS_LIST}

  INPUTS
    libarchive/libarchive/archive.h
    libarchive/libarchive/archive_entry.h

  LIBRARY archive_static
  LIBRARY_NAME Libarchive
  NAMESPACE ""
)

target_include_directories(libarchive_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"  # To find the generated SAPI header
)


add_subdirectory(examples)
