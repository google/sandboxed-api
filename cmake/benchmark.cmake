# Copyright 2019 Google LLC
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

set(workdir "${CMAKE_BINARY_DIR}/_deps/benchmark-populate")

set(SAPI_BENCHMARK_GIT_REPOSITORY https://github.com/google/benchmark.git
                                  CACHE STRING "")
set(SAPI_BENCHMARK_GIT_TAG 3b3de69400164013199ea448f051d94d7fc7d81f
                           CACHE STRING "") # 2021-12-14
set(SAPI_BENCHMARK_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/benchmark-src"
                              CACHE STRING "")
set(SAPI_BENCHMARK_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/benchmark-build"
                              CACHE STRING "")

file(WRITE "${workdir}/CMakeLists.txt" "\
cmake_minimum_required(VERSION ${CMAKE_VERSION})
project(benchmark-populate NONE)
include(ExternalProject)
ExternalProject_Add(benchmark
  GIT_REPOSITORY    \"${SAPI_BENCHMARK_GIT_REPOSITORY}\"
  GIT_TAG           \"${SAPI_BENCHMARK_GIT_TAG}\"
  SOURCE_DIR        \"${SAPI_BENCHMARK_SOURCE_DIR}\"
  BINARY_DIR        \"${SAPI_BENCHMARK_BINARY_DIR}\"
  CONFIGURE_COMMAND \"\"
  BUILD_COMMAND     \"\"
  INSTALL_COMMAND   \"\"
  TEST_COMMAND      \"\"
)
")

execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
                RESULT_VARIABLE error
                WORKING_DIRECTORY "${workdir}")
if(error)
  message(FATAL_ERROR "CMake step for ${PROJECT_NAME} failed: ${error}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} --build .
                RESULT_VARIABLE error
                WORKING_DIRECTORY "${workdir}")
if(error)
  message(FATAL_ERROR "Build step for ${PROJECT_NAME} failed: ${error}")
endif()

set(BENCHMARK_ENABLE_TESTING OFF)
set(BENCHMARK_ENABLE_EXCEPTIONS OFF)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF)

add_subdirectory("${SAPI_BENCHMARK_SOURCE_DIR}"
                 "${SAPI_BENCHMARK_BINARY_DIR}" EXCLUDE_FROM_ALL)
