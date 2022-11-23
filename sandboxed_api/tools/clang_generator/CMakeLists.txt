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

# Minimum supported: LLVM 7.0.1
find_package(LLVM REQUIRED)
find_package(Clang REQUIRED)
if(LLVM_VERSION VERSION_LESS "7.0.1")
  message(FATAL_ERROR "SAPI header generator needs LLVM 7.0.1 or newer")
endif()

add_library(sapi_generator
  diagnostics.cc
  diagnostics.h
  emitter.h
  emitter.cc
  generator.h
  generator.cc
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
llvm_map_components_to_libnames(_sapi_generator_llvm_libs
  core
)
target_link_libraries(sapi_generator PUBLIC
  sapi::base
  absl::algorithm_container
  absl::btree
  absl::flat_hash_set
  absl::node_hash_set
  absl::random_random
  absl::status
  absl::statusor
  absl::strings
  clang
  clang-cpp
  sapi::fileops
  sapi::status
  ${_sapi_generator_llvm_libs}
)

add_executable(sapi_generator_tool
  compilation_database.cc
  compilation_database.h
  generator_tool.cc
)
target_link_libraries(sapi_generator_tool PRIVATE
  sapi::base
  sapi::file_base
  sapi::file_helpers
  sapi::fileops
  sapi::generator
)

if(BUILD_TESTING AND SAPI_BUILD_TESTING)
  add_executable(sapi_generator_test
    frontend_action_test_util.cc
    frontend_action_test_util.h
    emitter_test.cc
  )
  target_link_libraries(sapi_generator_test PRIVATE
    absl::flat_hash_map
    absl::memory
    absl::statusor
    benchmark
    sapi::sapi
    sapi::generator
    sapi::status
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sapi_generator_test)
endif()
