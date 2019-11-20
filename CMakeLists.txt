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

project(SandboxedAPI C CXX ASM)

# SAPI-wide setting for the language level
set(SAPI_CXX_STANDARD 17)

set(SAPI_BINARY_DIR "${PROJECT_BINARY_DIR}" CACHE INTERNAL "" FORCE)
set(SAPI_SOURCE_DIR "${PROJECT_SOURCE_DIR}" CACHE INTERNAL "" FORCE)

include(CheckCXXCompilerFlag)

# Sapi CMake modules, order matters
list(APPEND CMAKE_MODULE_PATH "${SAPI_SOURCE_DIR}/cmake")
include(SapiOptions)
include(SapiDeps)
include(SapiUtil)
include(SapiBuildDefs)

# Make Bazel-style includes work
configure_file(cmake/libcap_capability.h.in
               libcap/include/sys/capability.h
               @ONLY)

# Library with basic project settings. The empty file is there to be able to
# define header-only libraries without cumbersome target_sources() hacks.
file(WRITE ${SAPI_BINARY_DIR}/sapi_base_force_cxx_linkage.cc "")
add_library(sapi_base STATIC
  "${SAPI_BINARY_DIR}/sapi_base_force_cxx_linkage.cc"
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
  "${SAPI_BINARY_DIR}"
  "${SAPI_SOURCE_DIR}"
  "${Protobuf_INCLUDE_DIR}"
  # Need to reach into Abseil internal headers from a few targets.
  "${CMAKE_BINARY_DIR}/absl-src"
)
if(UNIX)
  foreach(flag IN ITEMS -Wno-deprecated
                        -Wno-deprecated-declarations
                        -Wno-psabi)
    check_cxx_compiler_flag(${flag} _sapi_has_flag)
    if(_sapi_has_flag)
      target_compile_options(sapi_base INTERFACE
        ${flag}
      )
    endif()
  endforeach()
endif()

add_library(sapi_test_main INTERFACE)
add_library(sapi::test_main ALIAS sapi_test_main)
target_link_libraries(sapi_test_main INTERFACE
  gtest_main
  gmock
  sapi::base
)

if(SAPI_ENABLE_TESTS)
  include(GoogleTest)
  # Setup tests to work like with Bazel
  create_directory_symlink("${SAPI_BINARY_DIR}" com_google_sandboxed_api)
  enable_testing()
endif()

add_subdirectory(sandboxed_api)
