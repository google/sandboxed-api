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

project(jsonnet-sapi C CXX)
include(CTest)
include(GoogleTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   EXCLUDE_FROM_ALL)
endif()

FetchContent_Declare(jsonnet
  GIT_REPOSITORY https://github.com/google/jsonnet.git
  GIT_TAG        v0.18.0 # 2021-12-21
)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE) # Do not build jsonnet tests
FetchContent_MakeAvailable(jsonnet)
create_directory_symlink("${jsonnet_SOURCE_DIR}"
                         "${PROJECT_BINARY_DIR}/jsonnet")

configure_file("${jsonnet_SOURCE_DIR}/cmd/jsonnet.cpp"
               "${PROJECT_BINARY_DIR}/gen_files/jsonnet.cpp" COPYONLY)

add_custom_command(
  OUTPUT "${PROJECT_BINARY_DIR}/gen_files/write_helper.cc"
  WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/gen_files"
  COMMAND patch -o write_helper.cc
            < "${PROJECT_SOURCE_DIR}/jsonnet.patch" > /dev/null
)

add_library(jsonnet_helper STATIC
  "${PROJECT_BINARY_DIR}/gen_files/write_helper.cc"
  "${jsonnet_SOURCE_DIR}/cmd/utils.cpp"
  "${jsonnet_SOURCE_DIR}/cmd/utils.h"
  jsonnet_helper.cc
  jsonnet_helper.h
)
add_library(sapi_contrib::jsonnet_helper ALIAS jsonnet_helper)
target_include_directories(jsonnet_helper PUBLIC
  "${PROJECT_BINARY_DIR}"
  "${PROJECT_BINARY_DIR}/gen_files"
  "${SAPI_SOURCE_DIR}"
)
target_link_libraries(jsonnet_helper
  libjsonnet_for_binaries
)

add_sapi_library(jsonnet_sapi
  FUNCTIONS c_free_input
            c_jsonnet_destroy
            c_jsonnet_evaluate_snippet
            c_jsonnet_evaluate_snippet_multi
            c_jsonnet_evaluate_snippet_stream
            c_jsonnet_fmt_snippet
            c_jsonnet_make
            c_jsonnet_realloc
            c_read_input
            c_write_multi_output_files
            c_write_output_file
            c_write_output_stream
  INPUTS jsonnet_helper.h
  LIBRARY jsonnet_helper
  LIBRARY_NAME Jsonnet
  NAMESPACE ""
)
add_library(sapi_contrib::jsonnet ALIAS jsonnet_sapi)
target_include_directories(jsonnet_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"
)
target_link_libraries(jsonnet_sapi PUBLIC
  sapi_contrib::jsonnet_helper
)

if(SAPI_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(BUILD_TESTING AND SAPI_BUILD_TESTING)
  # Create directories so the tests will be able to access them
  file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/tests_input"
                      "${PROJECT_BINARY_DIR}/tests_output"
                      "${PROJECT_BINARY_DIR}/tests_expected_output")

  add_custom_target(test_preparation ALL
    COMMAND cp ${PROJECT_SOURCE_DIR}/examples/jsonnet_codes/*
              ${PROJECT_BINARY_DIR}/tests_input
    COMMAND cp ${PROJECT_SOURCE_DIR}/examples/jsonnet_codes_expected_output/*
              ${PROJECT_BINARY_DIR}/tests_expected_output
  )

  add_executable(jsonnet_tests
    jsonnet_tests.cc
  )
  target_include_directories(jsonnet_tests PUBLIC
    "${PROJECT_SOURCE_DIR}"
  )
  target_link_libraries(jsonnet_tests
    sapi_contrib::jsonnet
    sapi::test_main
  )
  gtest_discover_tests(jsonnet_tests)
endif()
