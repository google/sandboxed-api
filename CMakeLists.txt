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

cmake_minimum_required(VERSION 3.12)

# Fix Ninja generator output to not rebuild entire sub-trees needlessly.
if(CMAKE_GENERATOR MATCHES "Ninja")
  file(WRITE "${CMAKE_BINARY_DIR}/UserMakeRulesOverride.cmake"
    "string(REPLACE \"-MD\" \"-MMD\" CMAKE_DEPFILE_FLAGS_C \"\${CMAKE_DEPFILE_FLAGS_C}\")\n"
    "string(REPLACE \"-MD\" \"-MMD\" CMAKE_DEPFILE_FLAGS_CXX \"\${CMAKE_DEPFILE_FLAGS_CXX}\")\n"
  )
  set(CMAKE_USER_MAKE_RULES_OVERRIDE
      "${CMAKE_BINARY_DIR}/UserMakeRulesOverride.cmake" CACHE INTERNAL "")
endif()

project(SandboxedAPI C CXX ASM)

# SAPI-wide setting for the language level
set(SAPI_CXX_STANDARD 17)

set(SAPI_BINARY_DIR "${PROJECT_BINARY_DIR}" CACHE INTERNAL "" FORCE)
set(SAPI_SOURCE_DIR "${PROJECT_SOURCE_DIR}" CACHE INTERNAL "" FORCE)

set(CMAKE_POSITION_INDEPENDENT_CODE TRUE CACHE BOOL "" FORCE)

include(CheckCXXCompilerFlag)

# TODO(cblichmann): Only apply this to SAPI and its dependencies
add_compile_options(-fno-exceptions)

if(SAPI_HARDENED_SOURCE)
  add_compile_options(-fstack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2)
  add_link_options(-Wl,-z,relro -Wl,-z,now)
endif()

# Sapi CMake modules, order matters
list(APPEND CMAKE_MODULE_PATH "${SAPI_SOURCE_DIR}/cmake"
                              "${SAPI_SOURCE_DIR}/cmake/modules")
include(SapiOptions)
include(SapiDeps)
include(SapiUtil)
include(SapiBuildDefs)
include(GNUInstallDirs)

# Allow the header generator to auto-configure include paths
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
)
if(UNIX)
  set(_sapi_check_no_deprecated
    -Wno-deprecated SAPI_HAS_W_NO_DEPRECATED
  )
  # For sandbox2/util.cc's CloneAndJump()
  set(_sapi_check_frame_larger_than
    -Wframe-larger-than=20480 SAPI_HAS_W_FRAME_LARGER_THAN
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
      target_compile_options(sapi_base INTERFACE ${opt_value})
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
