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

cmake_minimum_required(VERSION 3.13)

project(sapi_libpcap CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   EXCLUDE_FROM_ALL)
endif()

set(HIDE_SYMBOLS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(libpcap
  GIT_REPOSITORY https://github.com/the-tcpdump-group/libpcap.git
  GIT_TAG        f3c285e5b644c1f36bd8500d14f7e6df9728940d
  PATCH_COMMAND  patch --batch -N < "${CMAKE_SOURCE_DIR}/patches/cmake.patch"
)
FetchContent_MakeAvailable(libpcap)

add_subdirectory(sapilibc)
configure_file(pcap.gen.h.in pcap.gen.h)

add_library(libpcap_bundle INTERFACE)
target_link_libraries(libpcap_bundle INTERFACE
  pcap_static
  sapilibc
)

add_sapi_library(
  sapi_libpcap

  FUNCTIONS
    sapi_fdopen
    sapi_fclose

    pcap_fopen_offline
    pcap_close

    pcap_next

    pcap_setfilter
    pcap_compile

  INPUTS
    "sapilibc/sapilibc.h"
    "${CMAKE_BINARY_DIR}/pcap.gen.h"

  LIBRARY libpcap_bundle
  LIBRARY_NAME Libpcap
  NAMESPACE ""
)
add_library(sapi_contrib::libpcap ALIAS sapi_libpcap)
target_include_directories(sapi_libpcap INTERFACE
  "${PROJECT_BINARY_DIR}"
  "${SAPI_SOURCE_DIR}"
)

if(SAPI_ENABLE_EXAMPLES)
  add_subdirectory(example)
endif()

if(SAPI_ENABLE_TESTS)
  add_subdirectory(test)
endif()
