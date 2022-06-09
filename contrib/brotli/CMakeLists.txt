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

project(sapi_brotli CXX)
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

FetchContent_Declare(libbrotli
  GIT_REPOSITORY https://github.com/google/brotli.git
  GIT_TAG        f4153a09f87cbb9c826d8fc12c74642bb2d879ea # 2022-01-10
)
FetchContent_MakeAvailable(libbrotli)

set(brotli_INCLUDE_DIR "${brotli_SOURCE_DIR}/c/include")
configure_file(brotli.gen.h.in brotli.gen.h)

add_library(brotli_static STATIC
  "${SAPI_BINARY_DIR}/sapi_force_cxx_linkage.cc"
  $<TARGET_OBJECTS:brotlicommon-static>
  $<TARGET_OBJECTS:brotlidec-static>
  $<TARGET_OBJECTS:brotlienc-static>
)

add_sapi_library(sapi_brotli
  FUNCTIONS BrotliDecoderCreateInstance
            BrotliDecoderSetParameter
            BrotliDecoderDecompressStream
            BrotliDecoderTakeOutput
            BrotliDecoderDestroyInstance

            BrotliEncoderCreateInstance
            BrotliEncoderCompressStream
            BrotliEncoderTakeOutput
            BrotliEncoderSetParameter
            BrotliEncoderDestroyInstance

  INPUTS "${CMAKE_BINARY_DIR}/brotli.gen.h"

  LIBRARY brotli_static
  LIBRARY_NAME Brotli
  NAMESPACE ""
)
add_library(sapi_contrib::brotli ALIAS sapi_brotli)
target_include_directories(sapi_brotli INTERFACE
  "${PROJECT_BINARY_DIR}"
  "${SAPI_SOURCE_DIR}"
)

if(SAPI_BUILD_EXAMPLES)
  add_subdirectory(example)
endif()

if(BUILD_TESTING AND SAPI_BUILD_TESTING)
  add_subdirectory(test)
endif()
