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

# Allows use target_link_libraries() with targets in other directories.
if(POLICY CMP0079)
  cmake_policy(SET CMP0079 NEW)
endif()

set(workdir "${CMAKE_BINARY_DIR}/_deps/glog-populate")

set(SAPI_GLOG_GIT_REPOSITORY https://github.com/google/glog.git CACHE STRING "")
set(SAPI_GLOG_GIT_TAG 3ba8976592274bc1f907c402ce22558011d6fc5e
                      CACHE STRING "") # 2020-02-16
set(SAPI_GLOG_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/glog-src" CACHE STRING "")
set(SAPI_GLOG_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/glog-build" CACHE STRING "")

file(WRITE "${workdir}/CMakeLists.txt" "\
cmake_minimum_required(VERSION ${CMAKE_VERSION})
project(glog-populate NONE)
include(ExternalProject)
ExternalProject_Add(glog
  GIT_REPOSITORY    \"${SAPI_GLOG_GIT_REPOSITORY}\"
  GIT_TAG           \"${SAPI_GLOG_GIT_TAG}\"
  SOURCE_DIR        \"${SAPI_GLOG_SOURCE_DIR}\"
  BINARY_DIR        \"${SAPI_GLOG_BINARY_DIR}\"
  CONFIGURE_COMMAND \"\"
  BUILD_COMMAND     \"\"
  INSTALL_COMMAND   \"\"
  TEST_COMMAND      \"\"
)
")

execute_process(COMMAND "${CMAKE_COMMAND}" -G "${CMAKE_GENERATOR}" .
                RESULT_VARIABLE error
                WORKING_DIRECTORY "${workdir}")
if(error)
  message(FATAL_ERROR "CMake step for ${PROJECT_NAME} failed: ${error}")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" --build .
                RESULT_VARIABLE error
                WORKING_DIRECTORY "${workdir}")
if(error)
  message(FATAL_ERROR "Build step for ${PROJECT_NAME} failed: ${error}")
endif()

set(_sapi_saved_BUILD_TESTING ${BUILD_TESTING})

# Force gflags from subdirectory
set(WITH_GFLAGS FALSE CACHE BOOL "" FORCE)
set(HAVE_LIB_GFLAGS TRUE CACHE STRING "" FORCE)

set(WITH_UNWIND FALSE CACHE BOOL "" FORCE)
set(UNWIND_LIBRARY FALSE)
set(HAVE_PWD_H FALSE)

set(WITH_PKGCONFIG TRUE CACHE BOOL "" FORCE)

set(BUILD_TESTING FALSE)
set(BUILD_SHARED_LIBS ${SAPI_ENABLE_SHARED_LIBS})

add_subdirectory("${SAPI_GLOG_SOURCE_DIR}"
                 "${SAPI_GLOG_BINARY_DIR}" EXCLUDE_FROM_ALL)

target_include_directories(glog PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/_deps/gflags-build/include>
  $<BUILD_INTERFACE:${SAPI_GLOG_BINARY_DIR}>
)
add_library(gflags_nothreads STATIC IMPORTED)
set_target_properties(gflags_nothreads PROPERTIES
  IMPORTED_LOCATION
  "${CMAKE_BINARY_DIR}/_deps/gflags-build/libgflags_nothreads.a")
target_link_libraries(glog PRIVATE
  -Wl,--whole-archive
  gflags_nothreads
  -Wl,--no-whole-archive
)

if(_sapi_saved_BUILD_TESTING)
  set(BUILD_TESTING "${_sapi_saved_BUILD_TESTING}")
endif()
