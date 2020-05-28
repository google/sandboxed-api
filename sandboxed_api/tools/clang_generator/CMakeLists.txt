# Copyright 2020 Google LLC
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

# Minimum supported: LLVM 9
find_package(LLVM REQUIRED)
find_package(Clang REQUIRED)

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
  absl::flat_hash_set
  absl::memory
  absl::random_random
  absl::status
  absl::strings
  clangFormat
  clangFrontendTool
  clangTooling
  sandbox2::fileops
  sapi::status
  ${_sapi_generator_llvm_libs}
)

add_executable(sapi_generator_tool
  generator_tool.cc
)
target_link_libraries(sapi_generator_tool PRIVATE
  sapi::base
  sandbox2::fileops
  sapi::generator
)

