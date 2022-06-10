# Copyright 2020 Google LLC
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

project(lodepng_sapi C CXX)
include(CTest)
include(GoogleTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED 17)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   EXCLUDE_FROM_ALL)
endif()

FetchContent_Declare(lodepng
  GIT_REPOSITORY https://github.com/lvandeve/lodepng.git
  GIT_TAG        3d9fda048393e32cc11d0c3d3caba0a85c1c2dfe # 2022-05-22
)
FetchContent_MakeAvailable(lodepng)

# lodepng can be compiled as both C++ and C. We want the latter, so enforce
# C as the language. set_source_files_properties() does not work here.
configure_file(lodepng.gen.h.in
              "${lodepng_BINARY_DIR}/lodepng.gen.h")
configure_file("${lodepng_SOURCE_DIR}/lodepng.cpp"
               "${lodepng_BINARY_DIR}/lodepng.c" COPYONLY)

# Build static library
add_library(lodepng STATIC
  "${lodepng_BINARY_DIR}/lodepng.c"
  "${lodepng_BINARY_DIR}/lodepng.gen.h"
  "${lodepng_SOURCE_DIR}/lodepng.h"
)
target_include_directories(lodepng PUBLIC
  "${lodepng_BINARY_DIR}"
  "${lodepng_SOURCE_DIR}"
)
target_compile_definitions(lodepng PUBLIC
  LODEPNG_NO_COMPILE_CPP
)

# Build SAPI library
add_sapi_library(lodepng_sapi
  FUNCTIONS lodepng_decode_memory
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

  INPUTS "${lodepng_BINARY_DIR}/lodepng.gen.h"
  LIBRARY lodepng
  LIBRARY_NAME Lodepng
  NAMESPACE ""
)
add_library(sapi_contrib::lodepng ALIAS lodepng_sapi)
target_include_directories(lodepng_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"  # To find the generated SAPI header
)

# Examples and tests
add_subdirectory(examples)
