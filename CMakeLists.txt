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

cmake_minimum_required(VERSION 3.10)

option(SAPI_USE_SUPERBUILD "Whether or not a superbuild should be invoked" ON)
if(SAPI_USE_SUPERBUILD)
  project(superbuild NONE)
  include(cmake/SapiSuperBuild.cmake)
  return()
endif()
project(sandboxed_api C CXX ASM)

# SAPI-wide setting for the language level
set(SAPI_CXX_STANDARD 11)

# Sapi CMake modules, order matters
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(SapiOptions)
include(SapiDeps)
include(SapiUtil)
include(SapiBuildDefs)

include(GoogleTest)

# Make Bazel-style includes work
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
    -Wno-psabi
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
