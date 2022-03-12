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

project(sapi_spng CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   EXCLUDE_FROM_ALL)
endif()

FetchContent_Declare(libzlib
  GIT_REPOSITORY https://github.com/madler/zlib
  GIT_TAG        cacf7f1d4e3d44d871b605da3b647f07d718623f
)
FetchContent_MakeAvailable(libzlib)

set(SPNG_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libspng
  GIT_REPOSITORY https://github.com/randy408/libspng/
  GIT_TAG        c61b888cb9c7f88551636fb9608bb1a8168d20c5
)
FetchContent_MakeAvailable(libspng)

add_subdirectory(sapilibc)

add_library(libspng_static_bundle INTERFACE)
target_link_libraries(libspng_static_bundle INTERFACE
  spng_static
  sapilibc
  zlibstatic
)

configure_file(spng.gen.h.in spng.gen.h)

add_sapi_library(
  sapi_libspng

  FUNCTIONS
    sapi_fdopen
    sapi_fclose

    spng_ctx_new

    spng_set_png_buffer

    spng_get_ihdr
    spng_set_ihdr

    spng_set_option
    spng_get_option

    spng_set_plte
    spng_get_plte

    spng_get_row_info

    spng_decoded_image_size
    spng_decode_image
    spng_decode_row

    spng_encode_image
    spng_encode_row
    spng_get_png_buffer

    spng_ctx_free

    spng_strerror

    spng_set_png_file
  INPUTS
    "sapilibc/sapilibc.h"
    "${CMAKE_BINARY_DIR}/spng.gen.h"

  LIBRARY libspng_static_bundle
  LIBRARY_NAME Libspng
  NAMESPACE ""
)
add_library(sapi_contrib::libspng ALIAS sapi_libspng)
target_include_directories(sapi_libspng INTERFACE
  "${PROJECT_BINARY_DIR}"
  "${SAPI_SOURCE_DIR}"
)

if(SAPI_ENABLE_TESTS)
  add_subdirectory(test)
endif()
