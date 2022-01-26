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

project(sapi_zopfli CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")

  add_subdirectory(
    "${SAPI_ROOT}"
    "${CMAKE_BINARY_DIR}/sandboxed-api-build"
    EXCLUDE_FROM_ALL
  )
endif()

FetchContent_Declare(
  libzopfli

  GIT_REPOSITORY https://github.com/google/zopfli.git
  GIT_TAG 831773bc28e318b91a3255fa12c9fcde1606058b
)
FetchContent_MakeAvailable(libzopfli)

add_sapi_library(
  sapi_zopfli

  FUNCTIONS
    ZopfliInitOptions

    ZopfliCompress

    ZopfliDeflate
    ZopfliZlibCompress
    ZopfliGzipCompress

  INPUTS
    ${libzopfli_SOURCE_DIR}/src/zopfli/deflate.h
    ${libzopfli_SOURCE_DIR}/src/zopfli/zlib_container.h
    ${libzopfli_SOURCE_DIR}/src/zopfli/gzip_container.h

  LIBRARY libzopfli
  LIBRARY_NAME Zopfli
  NAMESPACE ""
)
add_library(sapi_contrib::zopfli ALIAS sapi_zopfli)

target_include_directories(sapi_zopfli INTERFACE
  "${PROJECT_BINARY_DIR}"
)

if(SAPI_ENABLE_EXAMPLES)
  add_subdirectory(example)
endif()

if(SAPI_ENABLE_TESTS)
  add_subdirectory(test)
endif()
