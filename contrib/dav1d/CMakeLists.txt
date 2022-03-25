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
project(dav1d-sapi CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   EXCLUDE_FROM_ALL)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(dav1d REQUIRED IMPORTED_TARGET dav1d)

add_sapi_library(dav1d_sapi
  INPUTS
    "${dav1d_INCLUDEDIR}/dav1d/common.h"
    "${dav1d_INCLUDEDIR}/dav1d/picture.h"
    "${dav1d_INCLUDEDIR}/dav1d/dav1d.h"
    "${dav1d_INCLUDEDIR}/dav1d/data.h"
  LIBRARY dav1d
  FUNCTIONS
    dav1d_data_create
    dav1d_data_wrap
    dav1d_data_wrap_user_data
    dav1d_data_unref
    dav1d_version
    dav1d_open
    dav1d_parse_sequence_header
    dav1d_send_data
    dav1d_get_picture
    dav1d_close
    dav1d_flush
    dav1d_get_event_flags
    dav1d_picture_unref
  LIBRARY_NAME dav1d
  NAMESPACE "dav1d_sapi"
)
add_library(sapi_contrib::dav1d ALIAS dav1d_sapi)
target_include_directories(dav1d_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"
)
