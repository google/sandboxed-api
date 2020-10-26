# Copyright 2020 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http:#www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.10)

project(jsonnet-sapi C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_subdirectory(jsonnet)

add_subdirectory(examples)

set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
set(SAPI_ENABLE_EXAMPLES OFF CACHE BOOL "")
set(SAPI_ENABLE_TESTS OFF CACHE BOOL "")

add_subdirectory("${SAPI_ROOT}"
                 "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                 EXCLUDE_FROM_ALL)

add_sapi_library(jsonnet_sapi
  FUNCTIONS c_jsonnet_evaluate_snippet
            c_jsonnet_make
            c_jsonnet_destroy
            c_read_input
            c_write_output_file
            c_jsonnet_realloc
            c_jsonnet_evaluate_snippet_multi
            c_write_multi_output_files
            c_write_output_stream
            c_jsonnet_evaluate_snippet_stream
            c_jsonnet_fmt_snippet
  INPUTS jsonnet_helper.h
  LIBRARY jsonnet_helper
  LIBRARY_NAME Jsonnet
  NAMESPACE ""
)

target_include_directories(jsonnet_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"
)

target_link_libraries(jsonnet_sapi PUBLIC jsonnet_helper)

add_subdirectory(tests)
