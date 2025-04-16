# Copyright 2020 Google LLC
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

# Minimum supported: LLVM 18.0.0
find_package(LLVM REQUIRED)
find_package(Clang REQUIRED)
if(LLVM_VERSION VERSION_LESS "18.0.0")
  message(FATAL_ERROR "SAPI header generator needs LLVM 18 or newer")
endif()

add_library(sapi_generator
  diagnostics.cc
  diagnostics.h
  emitter.h
  emitter.cc
  emitter_base.h
  emitter_base.cc
  generator.h
  generator.cc
  includes.h
  types.h
  types.cc
)
add_library(sapi::generator ALIAS sapi_generator)
target_compile_definitions(sapi_generator PUBLIC
  ${LLVM_DEFINITIONS}
)
target_include_directories(sapi_generator PUBLIC
  ${LLVM_INCLUDE_DIRS}
)
list(APPEND _sapi_generator_llvm_comp
  Core
  BinaryFormat
  Remarks
  BitstreamReader
  Support
  Demangle
  FrontendOpenMP
  MC
  MCParser
  Option
  ProfileData
  Support
  BinaryFormat
  Demangle
)
list(APPEND _sapi_generator_llvm_comp
  WindowsDriver # Always needed
)
llvm_map_components_to_libnames(_sapi_generator_llvm_libs
  ${_sapi_generator_llvm_comp}
)
if(NOT SAPI_ENABLE_CLANG_TOOL_STATIC)
  # Use regular library name
  list(APPEND _sapi_generator_clang_libs clang-cpp)
else()
  foreach(_clang_lib IN ITEMS
    # Required Clang libraries
    clangBasic
    clangLex
    clangParse
    clangAST
    clangASTMatchers
    clangSema
    clangAnalysis
    clangEdit
    clangRewrite
    clangDriver
    clangSerialization
    clangFrontend
    clangToolingCore
    clangToolingInclusions
    clangTooling
    clangFormat
    clangSupport # Needed from LLVM 15 onwards
  )
    if(TARGET ${_clang_lib})
      list(APPEND _sapi_generator_clang_libs ${_clang_lib})
    endif()
  endforeach()
  # Remove dependency on dynamic library libLLVM.so, which is added by Clang.
  # Necessary symbols from LLVM are provided by _sapi_generator_llvm_libs.
  foreach(_clang_lib IN LISTS _sapi_generator_clang_libs)
    get_target_property(_clang_link ${_clang_lib} INTERFACE_LINK_LIBRARIES)
    if(_clang_link)
      list(REMOVE_ITEM _clang_link LLVM)
      set_target_properties(${_clang_lib} PROPERTIES
        INTERFACE_LINK_LIBRARIES "${_clang_link}"
      )
    endif()
  endforeach()
endif()
target_link_libraries(sapi_generator PUBLIC
  sapi::base
  absl::btree
  absl::flat_hash_set
  absl::node_hash_set
  absl::random_random
  absl::status
  absl::statusor
  absl::strings
  sapi::fileops
  sapi::status
  ${_sapi_generator_clang_libs}
  ${_sapi_generator_llvm_libs}
)

add_executable(sapi_generator_tool
  compilation_database.cc
  compilation_database.h
  generator_tool.cc
)
target_link_libraries(sapi_generator_tool PRIVATE
  absl::base
  absl::no_destructor
  absl::status
  absl::strings
  sapi::base
  sapi::file_base
  sapi::file_helpers
  sapi::fileops
  sapi::generator
)
if(SAPI_ENABLE_CLANG_TOOL_STATIC)
  target_link_options(sapi_generator_tool PRIVATE
    # These work for both GCC and Clang
    -static-libgcc
    -static-libstdc++
  )
endif()

if(BUILD_TESTING AND SAPI_BUILD_TESTING)
  add_executable(sapi_generator_test
    frontend_action_test_util.cc
    frontend_action_test_util.h
    emitter_test.cc
  )
  target_link_libraries(sapi_generator_test PRIVATE
    absl::check
    absl::flat_hash_map
    absl::memory
    absl::statusor
    benchmark
    sapi::file_base
    sapi::file_helpers
    sapi::runfiles
    sapi::sapi
    sapi::generator
    sapi::status
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sapi_generator_test
    ENVIRONMENT "TEST_DATA_DIR=${PROJECT_SOURCE_DIR}/testdata")
endif()
