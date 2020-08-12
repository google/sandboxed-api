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

cmake_minimum_required(VERSION 3.16)

project(lodepng_sapi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED 17)

add_library(lodepng STATIC lodepng/lodepng.cpp lodepng/lodepng.h)


# unsandboxed main
add_executable(lodepng_normal main.cc)
target_link_libraries(lodepng_normal PRIVATE lodepng)

# sandboxed main
set(SAPI_ROOT "/usr/local/google/home/amedar/internship/sandboxed-api" CACHE PATH "Path to the Sandboxed API source tree")

add_subdirectory("${SAPI_ROOT}"
                 "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                 # Omit this to have the full Sandboxed API in IDE
                 EXCLUDE_FROM_ALL)

add_sapi_library(
    lodepng_sapi
    FUNCTIONS lodepng_decode32_file
    lodepng_encode32_file
    lodepng_encode32
    lodepng_save_file
    lodepng_load_file
    lodepng_decode32
    INPUTS lodepng/lodepng.h
    LIBRARY lodepng
    LIBRARY_NAME Lodepng
    NAMESPACE ""
)


target_include_directories(lodepng_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"  # To find the generated SAPI header
)

add_executable(lodepng_sandboxed main_sandboxed.cc sandbox.h)

target_link_libraries(lodepng_sandboxed PRIVATE
  lodepng_sapi
  sapi::sapi
  sapi::vars
  sapi::status
  glog::glog
)

# gtest

include(GoogleTest)
enable_testing()

add_executable(main_unit_test
  main_unit_test.cc
  )

target_link_libraries(main_unit_test PRIVATE
  lodepng_sapi
  absl::memory
  absl::strings
  absl::time
  glog::glog
  sapi::flags
  sapi::sapi
  sapi::status
  sapi::test_main
  sapi::vars
)
gtest_discover_tests(main_unit_test)
