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

# Downloads and unpacks glog at configure time

# Allows use target_link_libraries() with targets in other directories.
if(POLICY CMP0079)
  cmake_policy(SET CMP0079 NEW)
endif()

set(workdir "${CMAKE_BINARY_DIR}/glog-download")

configure_file("${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.in"
               "${workdir}/CMakeLists.txt")
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

# Force gflags from subdirectory
set(WITH_GFLAGS OFF CACHE BOOL "" FORCE)
set(HAVE_LIB_GFLAGS 1 CACHE STRING "" FORCE)

set(WITH_UNWIND OFF CACHE BOOL "" FORCE)
set(UNWIND_LIBRARY FALSE)
set(HAVE_PWD_H FALSE)

set(WITH_PKGCONFIG ON CACHE BOOL "" FORCE)

set(_glog_BUILD_TESTING ${BUILD_TESTING})
set(BUILD_TESTING FALSE)
set(BUILD_SHARED_LIBS ${SAPI_ENABLE_SHARED_LIBS})
add_subdirectory("${CMAKE_BINARY_DIR}/glog-src"
                 "${CMAKE_BINARY_DIR}/glog-build" EXCLUDE_FROM_ALL)
set(BUILD_TESTING ${_glog_BUILD_TESTING})
set(_glog_BUILD_TESTING)
target_include_directories(glog PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/gflags-build/include>
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/glog-build>
)
add_library(gflags_nothreads STATIC IMPORTED)
set_target_properties(gflags_nothreads PROPERTIES
  IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/gflags-build/libgflags_nothreads.a")
target_link_libraries(glog PRIVATE
  -Wl,--whole-archive
  gflags_nothreads
  -Wl,--no-whole-archive
)
