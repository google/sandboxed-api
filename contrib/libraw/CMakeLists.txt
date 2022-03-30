# Copyright 2022 Google LLC
#
# Licensed under the Apache License, Version 2.0 
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

project(sapi_libraw CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(SAPI_ROOT "" CACHE PATH "Path to the Sandboxed API source tree")

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory(
    "${SAPI_ROOT}"
    "${CMAKE_BINARY_DIR}/sandboxed-api-build"
    EXCLUDE_FROM_ALL)
endif()

FetchContent_Declare(
  libraw

  GIT_REPOSITORY https://github.com/LibRaw/LibRaw.git
  GIT_TAG adcb898a00746c8aa886eb06cc9f5a1cb1834fca
)
FetchContent_GetProperties(libraw)
FetchContent_Populate(libraw)

set(LIBRAW_PATH "${libraw_SOURCE_DIR}" CACHE STRING "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  libraw_cmake

  GIT_REPOSITORY https://github.com/LibRaw/LibRaw-cmake.git
  GIT_TAG b82a1b0101b1e7264eb3113f1e6c1ba2372ebb7f
)
FetchContent_MakeAvailable(libraw_cmake)

configure_file(raw.gen.h.in raw.gen.h)

add_sapi_library(
  sapi_libraw

  FUNCTIONS
    libraw_init
    libraw_open_file
    libraw_unpack
    libraw_close

    libraw_subtract_black

    libraw_cameraList
    libraw_cameraCount

    libraw_COLOR

  INPUTS
    "${CMAKE_BINARY_DIR}/raw.gen.h"

  LIBRARY raw
  LIBRARY_NAME LibRaw
  NAMESPACE ""
)

target_include_directories(
  sapi_libraw INTERFACE

  "${PROJECT_BINARY_DIR}"
)

if(SAPI_ENABLE_EXAMPLES)
    add_subdirectory(example)
endif()

if(SAPI_ENABLE_TESTS)
    add_subdirectory(test)
endif()
