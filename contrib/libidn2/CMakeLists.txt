# Copyright 2021 Google LLC
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

cmake_minimum_required(VERSION 3.10)

project(test CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(PkgConfig REQUIRED)
pkg_check_modules(libidn2 REQUIRED IMPORTED_TARGET libidn2)

#set(SAPI_ROOT "${PROJECT_SOURCE_DIR}/../.." CACHE PATH "Path to the Sandboxed API source tree")

add_sapi_library(libidn2_sapi
  FUNCTIONS idn2_lookup_u8 idn2_register_u8
            idn2_strerror idn2_strerror_name
	    idn2_free idn2_to_ascii_8z
	    idn2_to_unicode_8z8z
  INPUTS /usr/include/idn2.h
  LIBRARY idn2
  LIBRARY_NAME IDN2
  NAMESPACE ""
)

target_include_directories(libidn2_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"
)

add_executable(simple
  simple.cc
)

target_link_options(simple PRIVATE -Wl,--allow-shlib-undefined)

target_link_libraries(simple PRIVATE
  libidn2_sapi
  idn2
  sapi::base
  gtest
)
