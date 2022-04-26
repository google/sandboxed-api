# Copyright 2022 Google LLC
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

project(turbojpeg-sapi CXX C)
include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   EXCLUDE_FROM_ALL)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(TURBOJPEG REQUIRED IMPORTED_TARGET libturbojpeg)

add_sapi_library(turbojpeg_sapi
  INPUTS "${TURBOJPEG_INCLUDEDIR}/turbojpeg.h"
  LIBRARY turbojpeg
  LIBRARY_NAME TurboJPEG
  NAMESPACE "turbojpeg_sapi"
)
add_library(sapi_contrib::turbojpeg ALIAS turbojpeg_sapi)
target_include_directories(turbojpeg_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"
)

if(BUILD_TESTING AND SAPI_BUILD_TESTING)
  add_subdirectory(tests)
endif()
