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

project(lodepng_sapi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED 17)

# Apply the patches to the header file.
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/patches")

# Move the original header to a new folder to use as target, apply the patch
# and move it back to build the library.
add_custom_command(
    OUTPUT "${PROJECT_BINARY_DIR}/patches/lodepng.h"
    COMMENT "Applying patch to header file."
    COMMAND cp "${PROJECT_SOURCE_DIR}/patches/header.patch" "${PROJECT_BINARY_DIR}/patches/"
    COMMAND cp "${PROJECT_SOURCE_DIR}/lodepng/lodepng.h" "${PROJECT_BINARY_DIR}/patches/"
    COMMAND cd "${PROJECT_BINARY_DIR}/patches" && patch < header.patch
    COMMAND cp "${PROJECT_BINARY_DIR}/patches/lodepng.h" "${PROJECT_SOURCE_DIR}/lodepng/"
)

set_property(
    SOURCE
    "${PROJECT_SOURCE_DIR}/lodepng/lodepng.h"
    APPEND PROPERTY OBJECT_DEPENDS
    "${PROJECT_BINARY_DIR}/patches/lodepng.h"
)


# Build static library.
add_library(lodepng STATIC
  "${PROJECT_SOURCE_DIR}/lodepng/lodepng.cpp"
  "${PROJECT_SOURCE_DIR}/lodepng/lodepng.h"
)

# Build SAPI library
set(SAPI_ROOT "" CACHE PATH "Path to the Sandboxed API source tree")


add_subdirectory("${SAPI_ROOT}"
  "${CMAKE_BINARY_DIR}/sandboxed-api-build"
  EXCLUDE_FROM_ALL
)

add_sapi_library(
  lodepng_sapi
  
  FUNCTIONS
  lodepng_decode_memory
  lodepng_decode32
  lodepng_decode24

  lodepng_decode_file
  lodepng_decode32_file
  lodepng_decode24_file

  lodepng_encode_memory
  lodepng_encode32
  lodepng_encode24

  lodepng_encode_file
  lodepng_encode32_file
  lodepng_encode24_file

  lodepng_save_file
  lodepng_load_file
  
  INPUTS "${PROJECT_SOURCE_DIR}/lodepng/lodepng.h"
  LIBRARY lodepng
  LIBRARY_NAME Lodepng
  NAMESPACE ""
)

target_include_directories(lodepng_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"  # To find the generated SAPI header
)

add_subdirectory(examples)
