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

set(workdir "${CMAKE_BINARY_DIR}/_deps/googletest-populate")

set(SAPI_GOOGLETEST_GIT_REPOSITORY https://github.com/google/googletest.git
                                   CACHE STRING "")
set(SAPI_GOOGLETEST_GIT_TAG 9a32aee22d771387c494be2d8519fbdf46a713b2
                            CACHE STRING "") # 2021-12-20
set(SAPI_GOOGLETEST_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/googletest-src"
                               CACHE STRING "")
set(SAPI_GOOGLETEST_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/googletest-build"
                               CACHE STRING "")

file(WRITE "${workdir}/CMakeLists.txt" "\
cmake_minimum_required(VERSION ${CMAKE_VERSION})
project(googletest-populate NONE)
include(ExternalProject)
ExternalProject_Add(googletest
  GIT_REPOSITORY    \"${SAPI_GOOGLETEST_GIT_REPOSITORY}\"
  GIT_TAG           \"${SAPI_GOOGLETEST_GIT_TAG}\"
  SOURCE_DIR        \"${SAPI_GOOGLETEST_SOURCE_DIR}\"
  BINARY_DIR        \"${SAPI_GOOGLETEST_BINARY_DIR}\"
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

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

add_subdirectory("${SAPI_GOOGLETEST_SOURCE_DIR}"
                 "${SAPI_GOOGLETEST_BINARY_DIR}" EXCLUDE_FROM_ALL)
