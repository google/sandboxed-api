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

project(libarchive_sapi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED 17)

# Build SAPI library
set(SAPI_ROOT "/usr/local/google/home/amedar/internship/sandboxed-api" CACHE PATH "Path to the Sandboxed API source tree")

add_subdirectory("${SAPI_ROOT}"
  "${CMAKE_BINARY_DIR}/sandboxed-api-build"
  EXCLUDE_FROM_ALL
)

add_sapi_library(
  libarchive_sapi
  
  FUNCTIONS
  archive_read_new
  
  INPUTS libarchive/libarchive/archive.h
  LIBRARY archive
  LIBRARY_NAME Libarchive
  NAMESPACE ""
)

target_include_directories(libarchive_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"  # To find the generated SAPI header
)


add_subdirectory(libarchive)
add_subdirectory(examples)