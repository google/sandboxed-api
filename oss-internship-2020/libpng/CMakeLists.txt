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

option(LIBPNG_SAPI_BUILD_EXAMPLES "" OFF)
option(LIBPNG_SAPI_BUILD_TESTING "" OFF)

set(SAPI_BUILD_EXAMPLES ${LIBPNG_SAPI_BUILD_EXAMPLES} CACHE BOOL "" FORCE)
set(SAPI_BUILD_TESTING ${LIBPNG_SAPI_BUILD_TESTING} CACHE BOOL "" FORCE)

set (CMAKE_FIND_LIBRARY_SUFFIXES .a $ {CMAKE_FIND_LIBRARY_SUFFIXES})
find_package(PNG REQUIRED)
list(GET PNG_INCLUDE_DIRS 0 PNG_INCLUDE_DIR)

add_subdirectory(wrapper)

add_subdirectory(
  "${SAPI_ROOT}"
  "${CMAKE_BINARY_DIR}/sandboxed-api-build"
  # Omit this to have the full Sandboxed API in IDE
  EXCLUDE_FROM_ALL
)

add_sapi_library(libpng_sapi
  # List of functions that we want to include in the
  # generated sandboxed API class

  FUNCTIONS png_image_free
            png_image_finish_read
            png_image_write_to_file
            png_image_begin_read_from_file

            png_get_rowbytes
            png_get_bit_depth
            png_get_color_type
            png_get_image_width
            png_get_image_height

            png_set_IHDR
            png_set_sig_bytes
            png_set_interlace_handling

            png_read_info
            png_read_image_wrapper
            png_read_update_info

            png_write_end
            png_write_info
            png_write_image_wrapper

            png_create_info_struct
            png_create_read_struct_wrapper
            png_create_write_struct_wrapper

            png_init_io_wrapper

            png_sig_cmp

            png_fread
            png_fdopen
            png_rewind
            png_fclose

            png_setjmp

  INPUTS "${PNG_INCLUDE_DIR}/png.h"
         wrapper/func.h
                          # Header files or .cc files that should be parsed
  LIBRARY wrapper
                          # Library dependency from the add_library() above
  LIBRARY_NAME LibPNG     # Name prefix for the generated header. Will be
                          # suffixed with "Api" and "Sandbox" as needed.
  NAMESPACE ""            # Optional C++ namespace to wrap the generated code
)

target_include_directories(libpng_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"  # To find the generated SAPI header
)

if (LIBPNG_SAPI_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if (LIBPNG_SAPI_BUILD_TESTING)
  add_subdirectory(tests)
endif()

