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

project(sapi_zstd CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   EXCLUDE_FROM_ALL)
endif()

FetchContent_Declare(libzstd
  GIT_REPOSITORY https://github.com/facebook/zstd.git
  GIT_TAG        4dfc4eca9a166b474b09542a9fc8e2da162eea99
  SOURCE_SUBDIR build/cmake
)
FetchContent_MakeAvailable(libzstd)

set(libzstd_INCLUDE_DIR "${libzstd_SOURCE_DIR}/lib")
add_subdirectory(wrapper)

add_sapi_library(
  sapi_zstd

  FUNCTIONS
    ZSTD_versionNumber
    ZSTD_versionString

    ZSTD_compressBound
    ZSTD_isError
    ZSTD_getErrorName
    ZSTD_minCLevel
    ZSTD_maxCLevel
    ZSTD_defaultCLevel

    ZSTD_createDCtx
    ZSTD_freeDCtx

    ZSTD_createCCtx
    ZSTD_freeCCtx

    ZSTD_CCtx_setParameter

    ZSTD_compress
    ZSTD_compressStream
    ZSTD_compressStream2
    ZSTD_flushStream
    ZSTD_endStream

    ZSTD_CStreamInSize
    ZSTD_CStreamOutSize

    ZSTD_decompress
    ZSTD_decompressStream

    ZSTD_getFrameContentSize

    ZSTD_compress_fd
    ZSTD_compressStream_fd

    ZSTD_decompress_fd
    ZSTD_decompressStream_fd
  INPUTS
    ${libzstd_INCLUDE_DIR}/zstd.h
    wrapper/wrapper_zstd.h

  LIBRARY wrapper_zstd
  LIBRARY_NAME Zstd
  NAMESPACE ""
)
add_library(sapi_contrib::zstd ALIAS sapi_zstd)
target_include_directories(sapi_zstd INTERFACE
  "${PROJECT_BINARY_DIR}"
  "${SAPI_SOURCE_DIR}"
)

if(SAPI_ENABLE_EXAMPLES)
  add_subdirectory(example)
endif()

if(SAPI_ENABLE_TESTS)
  add_subdirectory(test)
endif()
