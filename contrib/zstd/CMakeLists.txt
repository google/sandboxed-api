# SPDX-License-Identifier: Apache-2.0
# Copyright 2022 Google LLC
#                Mariusz Zaborski <oshogbo@invisiblethingslab.com>

cmake_minimum_required(VERSION 3.18)

project(sapi_zstd CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(SAPI_ROOT "" CACHE PATH "Path to the Sandboxed API source tree")
set(SAPI_ZSTD_ENABLE_EXAMPLES ON CACHE BOOL "")
set(SAPI_ZSTD_ENABLE_TESTS ON CACHE BOOL "")

include(FetchContent)
FetchContent_Declare(
  libzstd

  GIT_REPOSITORY https://github.com/facebook/zstd.git
  SOURCE_SUBDIR build/cmake
)
FetchContent_MakeAvailable(libzstd)
set(libzstd_INCLUDE_DIR "${libzstd_SOURCE_DIR}/lib")

add_subdirectory(
  "${SAPI_ROOT}"
  "${CMAKE_BINARY_DIR}/sandboxed-api-build"
  EXCLUDE_FROM_ALL
)

file(STRINGS sandboxed_functions.txt FUNCTIONS_LIST)

add_sapi_library(
  sapi_zstd

  FUNCTIONS
    ${FUNCTIONS_LIST}

  INPUTS
    ${libzstd_INCLUDE_DIR}/zstd.h

  LIBRARY libzstd_static
  LIBRARY_NAME Zstd
  NAMESPACE ""
)

target_include_directories(sapi_zstd INTERFACE
  "${PROJECT_BINARY_DIR}"
)

if (SAPI_ZSTD_ENABLE_EXAMPLES)
  add_subdirectory(example)
endif()

if (SAPI_ZSTD_ENABLE_TESTS)
  add_subdirectory(test)
endif()
