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

cmake_minimum_required(VERSION 3.13..3.22)

if(POLICY CMP0083)
  cmake_policy(SET CMP0083 NEW)
endif()

project(SandboxedAPI C CXX ASM)
include(CTest)

# TODO(cblichmann): Enable for Android once support lands
if(NOT CMAKE_SYSTEM_NAME MATCHES "Linux")
  message(FATAL_ERROR "Sandboxed API is only supported on Linux")
endif()

# SAPI-wide setting for the language level
set(SAPI_CXX_STANDARD 17)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD ${SAPI_CXX_STANDARD})
elseif(CMAKE_CXX_STANDARD LESS ${SAPI_CXX_STANDARD})
  message(FATAL_ERROR
    "Sandboxed API requires C++17. To ensure ABI compatibility"
    " build and link all targets of the project with the same"
    " version of C++."
  )
endif()

set(SAPI_BINARY_DIR "${PROJECT_BINARY_DIR}" CACHE INTERNAL "" FORCE)
set(SAPI_SOURCE_DIR "${PROJECT_SOURCE_DIR}" CACHE INTERNAL "" FORCE)

get_directory_property(_sapi_has_parent PARENT_DIRECTORY)
if(PROJECT_IS_TOP_LEVEL OR NOT _sapi_has_parent)
  set(SAPI_PROJECT_IS_TOP_LEVEL ON)
endif()

include(CheckCXXCompilerFlag)

# Allow the header generator to auto-configure include paths. Need to set a
# cache variable, so that sub- and super-projects also have this enabled.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")

set(CMAKE_SKIP_BUILD_RPATH ON)

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.14)
  include(CheckPIESupported)
  check_pie_supported()
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# SAPI CMake modules, order matters
list(APPEND CMAKE_MODULE_PATH "${SAPI_SOURCE_DIR}/cmake"
                              "${SAPI_SOURCE_DIR}/cmake/modules")
include(SapiOptions)
include(SapiDeps)
include(SapiUtil)
include(SapiBuildDefs)
include(GNUInstallDirs)

if(SAPI_HARDENED_SOURCE)
  add_compile_options(-fstack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2)
  add_link_options(-Wl,-z,relro -Wl,-z,now)
endif()

if(SAPI_FORCE_COLOR_OUTPUT)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")  # GCC
    add_compile_options(-fdiagnostics-color=always)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")  # Clang or Apple Clang
    add_compile_options(-fcolor-diagnostics)
  endif()
endif()

# Make Bazel-style includes work
configure_file(cmake/libcap_capability.h.in
               libcap/include/sys/capability.h
               @ONLY)

# Library with basic project settings. The empty file is there to be able to
# define header-only libraries without cumbersome target_sources() hacks.
configure_file(cmake/sapi_force_cxx_linkage.cc.in
               "${SAPI_BINARY_DIR}/sapi_force_cxx_linkage.cc" COPYONLY)
add_library(sapi_base STATIC
  "${SAPI_BINARY_DIR}/sapi_force_cxx_linkage.cc"
)
add_library(sapi::base ALIAS sapi_base)
target_compile_features(sapi_base PUBLIC
  cxx_std_${SAPI_CXX_STANDARD}
)
set_target_properties(sapi_base PROPERTIES
  INTERFACE_POSITION_INDEPENDENT_CODE ON
)
target_include_directories(sapi_base PUBLIC
  "${SAPI_BINARY_DIR}"
  "${SAPI_SOURCE_DIR}"
  "${Protobuf_INCLUDE_DIR}"
)
target_compile_options(sapi_base PUBLIC
  -fno-exceptions
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(sapi_base PUBLIC
    # The syscall tables in sandbox2/syscall_defs.cc are `std::array`s using
    # CTAD and have more entries than the default limit of 256.
    -fbracket-depth=768
  )
endif()
set(_sapi_check_no_deprecated
  -Wno-deprecated SAPI_HAS_W_NO_DEPRECATED
)
set(_sapi_check_frame_larger_than
  # For sandbox2/util.cc's CloneAndJump()
  -Wframe-larger-than=40960 SAPI_HAS_W_FRAME_LARGER_THAN
)
set(_sapi_check_no_deprecated_declarations
  -Wno-deprecated-declarations SAPI_HAS_W_NO_DEPRECATED_DECLARATIONS
)
set(_sapi_check_no_psabi
  -Wno-psabi SAPI_HAS_W_NO_PSABI
)
foreach(check IN ITEMS _sapi_check_no_deprecated
                       _sapi_check_frame_larger_than
                       _sapi_check_no_deprecated_declarations
                       _sapi_check_no_psabi)
  list(GET ${check} 0 opt_value)
  list(GET ${check} 1 var_name)
  check_cxx_compiler_flag(${opt_value} ${var_name})
  if(${var_name})
    target_compile_options(sapi_base PUBLIC ${opt_value})
  endif()
endforeach()

add_library(sapi_test_main INTERFACE)
add_library(sapi::test_main ALIAS sapi_test_main)
target_link_libraries(sapi_test_main INTERFACE
  gtest_main
  gmock
  sapi::base
)

if(BUILD_TESTING AND SAPI_BUILD_TESTING)
  include(GoogleTest)
  # Setup tests to work like with Bazel
  create_directory_symlink("${SAPI_BINARY_DIR}" com_google_sandboxed_api)
  enable_testing()
endif()

add_subdirectory(sandboxed_api)

if(BUILD_TESTING AND SAPI_BUILD_TESTING AND SAPI_CONTRIB_BUILD_TESTING)
  add_subdirectory(contrib)
endif()
