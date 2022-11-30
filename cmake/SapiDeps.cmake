# Copyright 2019 Google LLC
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

function(sapi_check_target target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR " SAPI: compiling Sandboxed API requires a ${target}
                   CMake target in your project")
  endif()
endfunction()

include(SapiFetchContent)

# Use static libraries
set(_sapi_saved_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
if (SAPI_ENABLE_SHARED_LIBS)
  set(SAPI_LIB_TYPE SHARED)
  set(CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_SHARED_LIBRARY_SUFFIX})
  set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
  # Imply linking with system-wide libs
  set(SAPI_DOWNLOAD_LIBCAP OFF CACHE BOOL "" FORCE)
  set(SAPI_DOWNLOAD_LIBFFI OFF CACHE BOOL "" FORCE)
  set(SAPI_DOWNLOAD_PROTOBUF OFF CACHE BOOL "" FORCE)
  set(SAPI_DOWNLOAD_ZLIB OFF CACHE BOOL "" FORCE)
  add_compile_definitions(SAPI_LIB_IS_SHARED=1)
else()
  set(SAPI_LIB_TYPE STATIC)
  set(CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_STATIC_LIBRARY_SUFFIX})
endif()

set(_sapi_saved_BUILD_TESTING ${BUILD_TESTING})
set(BUILD_TESTING OFF)  # No need to build test code of our deps

if(SAPI_BUILD_TESTING)
  if(SAPI_DOWNLOAD_GOOGLETEST)
    include(cmake/googletest.cmake)
  endif()
  sapi_check_target(gtest)
  sapi_check_target(gtest_main)
  sapi_check_target(gmock)

  if(SAPI_DOWNLOAD_BENCHMARK)
    include(cmake/benchmark.cmake)
  endif()
  sapi_check_target(benchmark)
endif()

if(SAPI_DOWNLOAD_ABSL)
  include(cmake/abseil-cpp.cmake)
endif()
sapi_check_target(absl::core_headers)

if(SAPI_DOWNLOAD_LIBCAP)
  include(cmake/libcap.cmake)
  sapi_check_target(libcap::libcap)
else()
  find_package(Libcap REQUIRED)
endif()

if(SAPI_DOWNLOAD_LIBFFI)
  include(cmake/libffi.cmake)
  sapi_check_target(libffi::libffi)
else()
  find_package(Libffi REQUIRED)
endif()

if(SAPI_DOWNLOAD_LIBUNWIND)
  include(cmake/libunwind.cmake)
endif()
sapi_check_target(unwind_ptrace)

if(SAPI_DOWNLOAD_PROTOBUF)
  include(cmake/protobuf.cmake)
endif()
find_package(Protobuf REQUIRED)

if(SAPI_BUILD_EXAMPLES)
  if(SAPI_DOWNLOAD_ZLIB)
    include(cmake/zlib.cmake)
    sapi_check_target(ZLIB::ZLIB)
  else()
    find_package(ZLIB REQUIRED)
  endif()
endif()

find_package(Threads REQUIRED)

if(NOT SAPI_ENABLE_GENERATOR)
  # Find Python 3 and add its location to the cache so that its available in
  # the add_sapi_library() macro in embedding projects.
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(SAPI_PYTHON3_EXECUTABLE "${Python3_EXECUTABLE}" CACHE INTERNAL "" FORCE)
endif()

# Undo global changes
if(_sapi_saved_BUILD_TESTING)
  set(BUILD_TESTING "${_sapi_saved_BUILD_TESTING}")
endif()
set(CMAKE_FIND_LIBRARY_SUFFIXES ${_sapi_saved_CMAKE_FIND_LIBRARY_SUFFIXES})
