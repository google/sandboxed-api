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

cmake_minimum_required(VERSION 3.12)

project(sandboxed_libpng CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set this on the command-line
set(SAPI_ROOT "" CACHE PATH "Path to the Sandboxed API source tree")
# To obtain a full SAPI_ROOT check out its source separately:
#   git clone https://github.com/google/sandboxed-api.git /path/to/sapi_root
# Then configure:
#   mkdir -p build && cd build
#   cmake .. -G Ninja -DSAPI_ROOT=/path/to/sapi_root

option(LIBPNG_SAPI_ENABLE_EXAMPLES "" OFF)
option(LIBPNG_SAPI_ENABLE_TESTS "" OFF)

set(SAPI_ENABLE_EXAMPLES ${LIBPNG_SAPI_ENABLE_EXAMPLES} CACHE BOOL "" FORCE)
set(SAPI_ENABLE_TESTS ${LIBPNG_SAPI_ENABLE_TESTS} CACHE BOOL "" FORCE)

#find_package(PNG REQUIRED)
#set(PNG_BUILD_ZLIB ON CACHE BOOL "" FORCE)

# To override lib option -- else SAPI won't work
#set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build OpenJPEG shared library and link executables against it." FORCE)
#add_subdirectory(libpng)

find_package(PNG REQUIRED)
list(GET PNG_INCLUDE_DIRS 0 PNG_INCLUDE_DIR)

add_subdirectory(
  "${SAPI_ROOT}"
  "${CMAKE_BINARY_DIR}/sandboxed-api-build"
  # Omit this to have the full Sandboxed API in IDE
  EXCLUDE_FROM_ALL
)

add_sapi_library(libpng_sapi
  # List of functions that we want to include in the
  # generated sandboxed API class

  FUNCTIONS   png_image_begin_read_from_file
              png_image_free
              png_image_finish_read
              png_image_write_to_file

  INPUTS "${PNG_INCLUDE_DIR}/png.h"
                          # Header files or .cc files that should be parsed
#  LIBRARY "${PNG_LIBRARY}"
  LIBRARY "/usr/lib/x86_64-linux-gnu/libpng16.a"
                          # Library dependency from the add_library() above
  LIBRARY_NAME LibPNG     # Name prefix for the generated header. Will be
                          # suffixed with "Api" and "Sandbox" as needed.
  NAMESPACE ""            # Optional C++ namespace to wrap the generated code
)

target_include_directories(libpng_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"  # To find the generated SAPI header
)

target_link_libraries(libpng_sapi.bin
  PRIVATE "/usr/lib/x86_64-linux-gnu/libz.a"
)

#if (LIBPNG_SAPI_ENABLE_EXAMPLES)
  add_subdirectory(examples)
#endif()

#if (LIBPNG_SAPI_ENABLE_TESTS)
#  add_subdirectory(test)
#endif()
