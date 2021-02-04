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

set(workdir "${CMAKE_BINARY_DIR}/_deps/protobuf-populate")

set(SAPI_PROTOBUF_GIT_REPOSITORY
  https://github.com/protocolbuffers/protobuf.git
  CACHE STRING "")
set(SAPI_PROTOBUF_GIT_TAG v3.11.4 CACHE STRING "") # 2020-02-14
set(SAPI_PROTOBUF_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/protobuf-src"
                             CACHE STRING "")
set(SAPI_PROTOBUF_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/protobuf-build"
                             CACHE STRING "")

file(WRITE "${workdir}/CMakeLists.txt" "\
cmake_minimum_required(VERSION ${CMAKE_VERSION})
project(protobuf-populate NONE)
include(ExternalProject)
ExternalProject_Add(protobuf
  GIT_REPOSITORY    \"${SAPI_PROTOBUF_GIT_REPOSITORY}\"
  GIT_TAG           \"${SAPI_PROTOBUF_GIT_TAG}\"
  GIT_SUBMODULES    \"cmake\" # Workaround for CMake #20579
  SOURCE_DIR        \"${SAPI_PROTOBUF_SOURCE_DIR}\"
  BINARY_DIR        \"${SAPI_PROTOBUF_BINARY_DIR}\"
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

set(protobuf_BUILD_TESTS FALSE CACHE BOOL "")
set(protobuf_BUILD_SHARED_LIBS FALSE CACHE BOOL "")
set(protobuf_WITH_ZLIB FALSE CACHE BOOL "")

add_subdirectory("${SAPI_PROTOBUF_SOURCE_DIR}/cmake"
                 "${SAPI_PROTOBUF_BINARY_DIR}" EXCLUDE_FROM_ALL)
get_property(Protobuf_INCLUDE_DIRS TARGET protobuf::libprotobuf
                                   PROPERTY INCLUDE_DIRECTORIES)
