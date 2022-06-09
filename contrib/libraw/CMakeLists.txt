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

project(sapi_libraw CXX)
include(CTest)
include(GoogleTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if (NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   EXCLUDE_FROM_ALL)
endif()

FetchContent_Declare(libraw
  GIT_REPOSITORY https://github.com/LibRaw/LibRaw.git
  GIT_TAG        a077aac05190530f22af4254a1e31745876d007f # 2022-06-03
)
FetchContent_MakeAvailable(libraw)

set(LIBRAW_PATH "${libraw_SOURCE_DIR}" CACHE STRING "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libraw_cmake
  GIT_REPOSITORY https://github.com/LibRaw/LibRaw-cmake.git
  GIT_TAG        b82a1b0101b1e7264eb3113f1e6c1ba2372ebb7f # 2021-11-18
)
FetchContent_MakeAvailable(libraw_cmake)

configure_file(raw.gen.h.in raw.gen.h)

add_sapi_library(sapi_libraw
  FUNCTIONS libraw_init
            libraw_open_file
            libraw_unpack
            libraw_close

            libraw_subtract_black

            libraw_cameraList
            libraw_cameraCount

            libraw_COLOR
            libraw_get_raw_height
            libraw_get_raw_width

  INPUTS "${CMAKE_BINARY_DIR}/raw.gen.h"

  LIBRARY raw
  LIBRARY_NAME LibRaw
  NAMESPACE ""
)
add_library(sapi_contrib::libraw ALIAS sapi_libraw)
target_include_directories(sapi_libraw INTERFACE
  "${PROJECT_BINARY_DIR}"
)

if(SAPI_BUILD_EXAMPLES)
  add_subdirectory(example)
endif()

if(BUILD_TESTING AND SAPI_BUILD_TESTING)
  add_subdirectory(test)
endif()
