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

cmake_minimum_required(VERSION 3.10)

project(openjpeg-sapi C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# To override lib option -- else SAPI won't work
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build OpenJPEG shared library and link executables against it." FORCE)
add_subdirectory(openjpeg)

set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
set(SAPI_BUILD_EXAMPLES OFF CACHE BOOL "")
set(SAPI_BUILD_TESTING OFF CACHE BOOL "")
set(EXECUTABLE_OUTPUT_PATH "" CACHE PATH "" FORCE)
add_subdirectory("${SAPI_ROOT}"
                 "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                 EXCLUDE_FROM_ALL)

add_sapi_library(openjp2_sapi
  FUNCTIONS opj_stream_destroy
            opj_stream_create_default_file_stream
            opj_create_decompress
            opj_image_destroy
            opj_setup_decoder
            opj_destroy_codec
            opj_read_header
            opj_decode
            opj_set_default_decoder_parameters
            opj_end_decompress

  INPUTS ${CMAKE_CURRENT_SOURCE_DIR}/openjpeg/src/lib/openjp2/openjpeg.h
  LIBRARY openjp2
  LIBRARY_NAME Openjp2
  NAMESPACE ""
)

target_include_directories(openjp2_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"
)

add_subdirectory(examples)
