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

set(workdir "${CMAKE_BINARY_DIR}/_deps/zlib-populate")

set(SAPI_ZLIB_URL https://mirror.bazel.build/zlib.net/zlib-1.2.11.tar.gz
                  CACHE STRING "")
set(SAPI_ZLIB_URL_HASH
  SHA256=c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1
  CACHE STRING "") # 2020-04-23
set(SAPI_ZLIB_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/zlib-src" CACHE STRING "")
set(SAPI_ZLIB_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/zlib-build" CACHE STRING "")

file(WRITE "${workdir}/CMakeLists.txt" "\
cmake_minimum_required(VERSION ${CMAKE_VERSION})
project(zlib-populate NONE)
include(ExternalProject)
ExternalProject_Add(zlib
  URL           \"${SAPI_ZLIB_URL}\"
  URL_HASH      \"${SAPI_ZLIB_URL_HASH}\"
  SOURCE_DIR    \"${SAPI_ZLIB_SOURCE_DIR}\"
  BINARY_DIR    \"${SAPI_ZLIB_BINARY_DIR}\"
  PATCH_COMMAND patch -p1
                < \"${SAPI_SOURCE_DIR}/sandboxed_api/bazel/external/zlib.patch\"
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

set(ZLIB_FOUND TRUE)
set(ZLIB_INCLUDE_DIRS ${SAPI_ZLIB_SOURCE_DIR})

add_library(z STATIC
  ${SAPI_ZLIB_SOURCE_DIR}/adler32.c
  ${SAPI_ZLIB_SOURCE_DIR}/compress.c
  ${SAPI_ZLIB_SOURCE_DIR}/crc32.c
  ${SAPI_ZLIB_SOURCE_DIR}/crc32.h
  ${SAPI_ZLIB_SOURCE_DIR}/deflate.c
  ${SAPI_ZLIB_SOURCE_DIR}/deflate.h
  ${SAPI_ZLIB_SOURCE_DIR}/gzclose.c
  ${SAPI_ZLIB_SOURCE_DIR}/gzguts.h
  ${SAPI_ZLIB_SOURCE_DIR}/gzlib.c
  ${SAPI_ZLIB_SOURCE_DIR}/gzread.c
  ${SAPI_ZLIB_SOURCE_DIR}/gzwrite.c
  ${SAPI_ZLIB_SOURCE_DIR}/infback.c
  ${SAPI_ZLIB_SOURCE_DIR}/inffast.c
  ${SAPI_ZLIB_SOURCE_DIR}/inffast.h
  ${SAPI_ZLIB_SOURCE_DIR}/inffixed.h
  ${SAPI_ZLIB_SOURCE_DIR}/inflate.c
  ${SAPI_ZLIB_SOURCE_DIR}/inflate.h
  ${SAPI_ZLIB_SOURCE_DIR}/inftrees.c
  ${SAPI_ZLIB_SOURCE_DIR}/inftrees.h
  ${SAPI_ZLIB_SOURCE_DIR}/trees.c
  ${SAPI_ZLIB_SOURCE_DIR}/trees.h
  ${SAPI_ZLIB_SOURCE_DIR}/uncompr.c
  ${SAPI_ZLIB_SOURCE_DIR}/zconf.h
  ${SAPI_ZLIB_SOURCE_DIR}/zlib.h
  ${SAPI_ZLIB_SOURCE_DIR}/zutil.c
  ${SAPI_ZLIB_SOURCE_DIR}/zutil.h
)
add_library(ZLIB::ZLIB ALIAS z)
target_include_directories(z PUBLIC
  ${SAPI_ZLIB_SOURCE_DIR}
)
target_compile_options(z PRIVATE
  -w
  -Dverbose=-1
)
target_link_libraries(z PRIVATE
  sapi::base
)
