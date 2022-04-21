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

# Example that demonstrates how to embed Sandboxed API into a project using
# CMake.

cmake_minimum_required(VERSION 3.12)

project(hello_sapi_project CXX)

# Path to the Sandboxed API source tree. Unlike Bazel, CMake does not download
# downstream dependencies by default. So the option below needs to be adjusted
# to point to a local checkout or a Git submodule.
# The default value is chosen so that this example can be easily tried out for
# a regular checkout of Sandboxed API.
set(SAPI_ROOT "${PROJECT_SOURCE_DIR}/../../.."
    CACHE PATH "Path to the Sandboxed API source tree")

# Configure options and include Sandboxed API as a sub-directory.
set(SAPI_BUILD_EXAMPLES OFF CACHE BOOL "")
set(SAPI_BUILD_TESTING OFF CACHE BOOL "")
add_subdirectory("${SAPI_ROOT}"
                 "${CMAKE_BINARY_DIR}/sandboxed-api-build" EXCLUDE_FROM_ALL)

# Interface library with common settings for this projects
add_library(hello_base INTERFACE)
add_library(hello::base ALIAS hello_base)
target_compile_features(hello_base INTERFACE cxx_std_17)
target_include_directories(hello_base INTERFACE
  "${PROJECT_BINARY_DIR}"  # To find the generated SAPI header
)

# Library with code that should be sandboxed
add_library(hello_lib STATIC
  hello_lib.cc
)
target_link_libraries(hello_lib PRIVATE
  hello::base
)

# Sandboxed API for the library above
add_sapi_library(hello_sapi
  FUNCTIONS AddTwoIntegers
  INPUTS hello_lib.cc
  LIBRARY hello_lib
  LIBRARY_NAME Hello
  NAMESPACE ""
)
add_library(hello::sapi ALIAS hello_sapi)

# Main executable demonstrating how the sandboxed library is used
add_executable(hello
  hello_main.cc
)
target_link_libraries(hello PRIVATE
  hello::base
  hello::sapi
  sapi::sapi
)

# Another example using the same library, but using the Transaction API that
# automatically retries sandbox operations. Also demonstates error handling
# and a custom security policy.
add_executable(hello_transacted
  hello_transacted.cc
)
target_link_libraries(hello_transacted PRIVATE
  hello::base
  hello::sapi
  sapi::sapi
)
