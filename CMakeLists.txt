# Copyright 2019 Google LLC. All Rights Reserved.
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

cmake_minimum_required(VERSION 3.5)

option(USE_SUPERBUILD "Whether or not a superbuild should be invoked" ON)
if(USE_SUPERBUILD)
  project(superbuild NONE)
  include(cmake/SuperBuild.cmake)
  return()
endif()
project(sandboxed_api C CXX ASM)

# SAPI-wide setting for the language level
set(SAPI_CXX_STANDARD 11)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_PREFIX_PATH
  "${PROJECT_BINARY_DIR}/Dependencies/Build/gflags"
  "${PROJECT_BINARY_DIR}/Dependencies/Build/glog"
  "${PROJECT_BINARY_DIR}/Dependencies/Build/protobuf"
)

include(SapiUtil)
include(GoogleTest)

# Build Abseil directly, as recommended upstream
find_path(absl_src_dir
  absl/base/port.h
  HINTS ${ABSL_ROOT_DIR}
  PATHS ${PROJECT_BINARY_DIR}/Dependencies/Source/absl
)
set(_sapi_saved_CMAKE_CXX_STANDARD ${CMAKE_CXX_STANDARD})
set(CMAKE_CXX_STANDARD ${SAPI_CXX_STANDARD})
add_subdirectory(${absl_src_dir}
                 ${PROJECT_BINARY_DIR}/Dependencies/Build/absl
                 EXCLUDE_FROM_ALL)
if(_sapi_saved_CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD "${_sapi_saved_CMAKE_CXX_STANDARD}")
endif()

# Build Googletest directly, as recommended upstream
find_path(googletest_src_dir
  googletest/include/gtest/gtest.h
  HINTS ${GOOGLETEST_ROOT_DIR}
  PATHS ${PROJECT_BINARY_DIR}/Dependencies/Source/googletest
)
set(gtest_force_shared_crt ON CACHE BOOL "")
add_subdirectory(${googletest_src_dir}
                 ${PROJECT_BINARY_DIR}/Dependencies/Build/googletest
                 EXCLUDE_FROM_ALL)

# Prefer to use static libraries
set(_sapi_saved_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
if(WIN32)
  set(CMAKE_FIND_LIBRARY_SUFFIXES .lib .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
else()
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
endif()

find_package(gflags REQUIRED)
find_package(glog REQUIRED)
find_package(Libcap REQUIRED)
find_package(Libffi REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Protobuf REQUIRED)

set(CMAKE_FIND_LIBRARY_SUFFIXES ${_sapi_saved_CMAKE_FIND_LIBRARY_SUFFIXES})

# Make Bazel-like includes work
configure_file(cmake/libcap_capability.h.in
               libcap/include/sys/capability.h
               @ONLY)
set(libunwind_INCLUDE_DIR
    ${PROJECT_BINARY_DIR}/Dependencies/Source/libunwind/include)
configure_file(cmake/libunwind_ptrace.h.in
               libunwind-ptrace.h
               @ONLY)

# Library with basic project settings. The empty file is there to be able to
# define header-only libraries without cumbersome target_sources() hacks.
file(TOUCH ${PROJECT_BINARY_DIR}/sapi_base_force_cxx_linkage.cc)
add_library(sapi_base STATIC
  ${PROJECT_BINARY_DIR}/sapi_base_force_cxx_linkage.cc
)
add_library(sapi::base ALIAS sapi_base)
set_target_properties(sapi_base PROPERTIES
  CXX_STANDARD              ${SAPI_CXX_STANDARD}
  CXX_STANDARD_REQUIRED     TRUE
  CXX_EXTENSIONS            FALSE
  SKIP_BUILD_RPATH          TRUE
  POSITION_INDEPENDENT_CODE TRUE
)
target_include_directories(sapi_base INTERFACE
  ${PROJECT_BINARY_DIR}
  ${PROJECT_SOURCE_DIR}
  ${Protobuf_INCLUDE_DIR}
  # Need to reach into Abseil internal headers from a few targets.
  ${PROJECT_BINARY_DIR}/Dependencies/Source/absl
)
if(UNIX)
  target_compile_options(sapi_base INTERFACE
    -Wno-deprecated
  )
endif()

add_library(sapi_test_main INTERFACE)
add_library(sapi::test_main ALIAS sapi_test_main)
target_link_libraries(sapi_test_main INTERFACE
  gtest_main
  gmock
  sapi::base
)

# Setup tests to work like with Bazel
create_directory_symlink(${PROJECT_BINARY_DIR} com_google_sandboxed_api)
enable_testing()

add_subdirectory(cmake/libunwind)
add_subdirectory(sandboxed_api)
