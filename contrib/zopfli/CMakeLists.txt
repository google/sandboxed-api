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

project(sapi_zopfli CXX)
include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   EXCLUDE_FROM_ALL)
endif()

FetchContent_Declare(zopfli
  GIT_REPOSITORY https://github.com/google/zopfli.git
  GIT_TAG 831773bc28e318b91a3255fa12c9fcde1606058b
)
FetchContent_MakeAvailable(zopfli)

add_subdirectory(wrapper)

add_sapi_library(
  sapi_zopfli

  FUNCTIONS
    ZopfliInitOptions

    ZopfliCompress

    ZopfliDeflate
    ZopfliZlibCompress
    ZopfliGzipCompress

    ZopfliCompressFD
  INPUTS
    "${zopfli_SOURCE_DIR}/src/zopfli/deflate.h"
    "${zopfli_SOURCE_DIR}/src/zopfli/gzip_container.h"
    "${zopfli_SOURCE_DIR}/src/zopfli/zlib_container.h"
    wrapper/wrapper_zopfli.h

  LIBRARY wrapper_zopfli
  LIBRARY_NAME Zopfli
  NAMESPACE ""
)
add_library(sapi_contrib::zopfli ALIAS sapi_zopfli)
target_include_directories(sapi_zopfli INTERFACE
  "${PROJECT_BINARY_DIR}"
  "${SAPI_SOURCE_DIR}"
)

if(SAPI_BUILD_EXAMPLES)
  add_subdirectory(example)
endif()

if(BUILD_TESTING AND SAPI_BUILD_TESTING)
  add_subdirectory(test)
endif()
