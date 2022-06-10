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

project(sapi_blosc CXX)
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

FetchContent_Declare(libblosc
  GIT_REPOSITORY https://github.com/Blosc/c-blosc.git
  GIT_TAG        ad6361f0151f830efb5ae113211c3559ab969886 # 2022-05-31
)
set(HIDE_SYMBOLS OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libblosc)

add_sapi_library(sapi_blosc
  FUNCTIONS blosc_init
            blosc_destroy

            blosc_compress
            blosc_decompress

            blosc_get_nthreads
            blosc_set_nthreads

            blosc_get_compressor
            blosc_set_compressor

            blosc_list_compressors

            blosc_get_version_string

            blosc_get_blocksize
            blosc_set_blocksize

            blosc_set_splitmode

            blosc_cbuffer_sizes
            blosc_cbuffer_validate
            blosc_cbuffer_versions
  INPUTS "${libblosc_SOURCE_DIR}/blosc/blosc.h"
  LIBRARY blosc_static
  LIBRARY_NAME Cblosc
  NAMESPACE ""
)
add_library(sapi_contrib::blosc ALIAS sapi_blosc)
target_include_directories(sapi_blosc INTERFACE
  "${PROJECT_BINARY_DIR}"
)

if (SAPI_BUILD_EXAMPLES)
  add_subdirectory(example)
endif()

if (BUILD_TESTING AND SAPI_BUILD_TESTING)
  add_subdirectory(test)
endif()
